#include "bls/bls_manager.h"

#include <dkg/dkg.h>
#include <libbls/bls/BLSPrivateKey.h>
#include <libbls/bls/BLSPrivateKeyShare.h>
#include <libbls/bls/BLSPublicKey.h>
#include <libbls/bls/BLSPublicKeyShare.h>
#include <libbls/tools/utils.h>
#include <libff/common/profiling.hpp>

#include "bls/bls_sign.h"
#include "common/global_info.h"
#include "network/route.h"
#include "network/network_utils.h"
#include "protos/prefix_db.h"
#include "init/init_utils.h"

namespace zjchain {

namespace bls {

void initLibSnark() noexcept {
    static bool s_initialized = []() noexcept
    {
        libff::inhibit_profiling_info = true;
        libff::inhibit_profiling_counters = true;
        libff::alt_bn128_pp::init_public_params();
        return true;
    }();
    (void)s_initialized;
}

void BlsManager::ProcessNewElectBlock(
        bool this_node_elected,
        uint32_t network_id,
        uint64_t elect_height,
        common::MembersPtr& new_members) {
    {
        auto iter = finish_networks_map_.find(network_id);
        if (iter != finish_networks_map_.end()) {
            finish_networks_map_.erase(iter);
        }
    }

    if (!this_node_elected) {
        return;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if (waiting_bls_ != nullptr) {
        waiting_bls_->Destroy();
        waiting_bls_.reset();
    }

    waiting_bls_ = std::make_shared<bls::BlsDkg>();
    waiting_bls_->Init(
        this,
        security_,
        0,
        0,
        libff::alt_bn128_Fr::zero(),
        libff::alt_bn128_G2::zero(),
        libff::alt_bn128_G2::zero(),
        db_);
    waiting_bls_->OnNewElectionBlock(elect_height, new_members);
    BLS_DEBUG("success add new bls dkg, elect_height: %lu", elect_height);
}

void BlsManager::SetUsedElectionBlock(
        uint64_t elect_height,
        uint32_t network_id,
        uint32_t member_count,
        const libff::alt_bn128_G2& common_public_key) try {
    std::lock_guard<std::mutex> guard(mutex_);
    if (max_height_ != common::kInvalidUint64 && elect_height <= max_height_) {
        BLS_ERROR("elect_height error: %lu, %lu", elect_height, max_height_);
        return;
    }

    max_height_ = elect_height;
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

int BlsManager::Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& local_sec_key,
        const std::string& sign_msg,
        libff::alt_bn128_G1* bn_sign) {
    BlsSign::Sign(t, n, local_sec_key, sign_msg, bn_sign);
    std::string sec_key = libBLS::ThresholdUtils::fieldElementToString(local_sec_key);
    return kBlsSuccess;
}

int BlsManager::Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& local_sec_key,
        const std::string& sign_msg,
        std::string* sign_x,
        std::string* sign_y) try {
//     std::lock_guard<std::mutex> guard(sign_mutex_);
    libff::alt_bn128_G1 bn_sign;
    BlsSign::Sign(t, n, local_sec_key, sign_msg, &bn_sign);
    bn_sign.to_affine_coordinates();
    *sign_x = libBLS::ThresholdUtils::fieldElementToString(bn_sign.X);
    *sign_y = libBLS::ThresholdUtils::fieldElementToString(bn_sign.Y);
//     std::string sec_key = libBLS::ThresholdUtils::fieldElementToString(local_sec_key);
//     BLSPublicKeyShare pkey(local_sec_key, t, n);
//     std::shared_ptr< std::vector< std::string > > strs = pkey.toString();
//     BLS_DEBUG("sign t: %u, , n: %u, , pk: %s,%s,%s,%s, sign x: %s, sign y: %s, sign msg: %s",
//         t, n, strs->at(0).c_str(), strs->at(1).c_str(),
//         strs->at(2).c_str(), strs->at(3).c_str(), (*sign_x).c_str(), (*sign_y).c_str(),
//         common::Encode::HexEncode(sign_msg).c_str());
    return kBlsSuccess;
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
    return kBlsError;
}

int BlsManager::Verify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& pubkey,
        const libff::alt_bn128_G1& sign,
        const std::string& sign_msg,
        std::string* verify_hash) try {
    if (pubkey == libff::alt_bn128_G2::zero()) {
        auto sign_ptr = const_cast<libff::alt_bn128_G1*>(&sign);
        sign_ptr->to_affine_coordinates();
        auto sign_x = libBLS::ThresholdUtils::fieldElementToString(sign_ptr->X);
        auto sign_y = libBLS::ThresholdUtils::fieldElementToString(sign_ptr->Y);
        BLS_ERROR("public key error: zero,msg hash: %s, sign x: %s, sign y: %s",
            common::Encode::HexEncode(sign_msg).c_str(),
            sign_x.c_str(),
            sign_y.c_str());
        return kBlsError;
    }

    //     std::lock_guard<std::mutex> guard(sign_mutex_);
    if (sign_msg.size() != 32) {
        BLS_ERROR("sign message error: %s", common::Encode::HexEncode(sign_msg));
        return kBlsError;
    }

    return BlsSign::Verify(t, n, sign, sign_msg, pubkey, verify_hash);
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
    return kBlsError;
}

int BlsManager::GetVerifyHash(
            uint32_t t,
            uint32_t n,
            const std::string& sign_msg,
            const libff::alt_bn128_G2& pkey,
            std::string* verify_hash) try {
    if (pkey == libff::alt_bn128_G2::zero()) {
        return kBlsError;
    }

    //     std::lock_guard<std::mutex> guard(sign_mutex_);
    if (sign_msg.size() != 32) {
        BLS_ERROR("sign message error: %s", common::Encode::HexEncode(sign_msg));
        return kBlsError;
    }

    return BlsSign::GetVerifyHash(t, n, sign_msg, pkey, verify_hash);
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
    return kBlsError;
}
void BlsManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (bls_msg.has_finish_req()) {
        HandleFinish(msg_ptr);
        return;
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (waiting_bls_ != nullptr) {
            waiting_bls_->HandleMessage(msg_ptr);
        }
    }
}

bool BlsManager::IsSignValid(
        const common::MembersPtr& members,
        const protobuf::BlsMessage& bls_msg,
        std::string* content_to_hash) {
    for (int32_t i = 0; i < bls_msg.finish_req().bitmap_size(); ++i) {
        *content_to_hash += std::to_string(bls_msg.finish_req().bitmap(i));
    }

    *content_to_hash += std::string("_") + std::to_string(bls_msg.finish_req().network_id());
    *content_to_hash = common::Hash::keccak256(*content_to_hash);
    auto& pubkey = (*members)[bls_msg.index()]->pubkey;
    if (!security_->Verify(*content_to_hash, bls_msg.sign(), pubkey)) {
        return false;
    }

    return true;
}

void BlsManager::HandleFinish(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (bls_msg.finish_req().network_id() < network::kRootCongressNetworkId ||
            bls_msg.finish_req().network_id() >= network::kConsensusShardEndNetworkId) {
        return;
    }

    common::MembersPtr members = nullptr;
//     elect::ElectManager::Instance()->GetWaitingNetworkMembers(
//         bls_msg.finish_req().network_id());
    if (members == nullptr || bls_msg.index() >= members->size()) {
        BLS_ERROR("not get waiting network members network id: %u, index: %d",
            bls_msg.finish_req().network_id(), bls_msg.index());
        return;
    }

    std::string msg_hash;
    if (!IsSignValid(members, bls_msg, &msg_hash)) {
        BLS_ERROR("IsSignValid failed network id: %u",
            bls_msg.finish_req().network_id());
        return;
    }

    std::vector<std::string> pkey_str = {
            bls_msg.finish_req().pubkey().x_c0(),
            bls_msg.finish_req().pubkey().x_c1(),
            bls_msg.finish_req().pubkey().y_c0(),
            bls_msg.finish_req().pubkey().y_c1()
    };
    auto t = common::GetSignerCount(members->size());
    BLSPublicKey pkey(std::make_shared<std::vector<std::string>>(pkey_str));
    std::vector<std::string> common_pkey_str = {
            bls_msg.finish_req().common_pubkey().x_c0(),
            bls_msg.finish_req().common_pubkey().x_c1(),
            bls_msg.finish_req().common_pubkey().y_c0(),
            bls_msg.finish_req().common_pubkey().y_c1()
    };
    BLSPublicKey common_pkey(std::make_shared<std::vector<std::string>>(common_pkey_str));
    std::string common_pk_str;
    for (uint32_t i = 0; i < common_pkey_str.size(); ++i) {
        common_pk_str += common_pkey_str[i];
    }

    std::string cpk_hash = common::Hash::Hash256(common_pk_str);
    libff::alt_bn128_G1 sign;
    sign.X = libff::alt_bn128_Fq(bls_msg.finish_req().bls_sign_x().c_str());
    sign.Y = libff::alt_bn128_Fq(bls_msg.finish_req().bls_sign_y().c_str());
    sign.Z = libff::alt_bn128_Fq::one();
    std::string verify_hash;
    if (Verify(
            t,
            members->size(),
            *pkey.getPublicKey(),
            sign,
            msg_hash,
            &verify_hash) != bls::kBlsSuccess) {
        ZJC_ERROR("verify bls finish bls sign error!");
        return;
    }

    BlsFinishItemPtr finish_item = nullptr;
    auto iter = finish_networks_map_.find(bls_msg.finish_req().network_id());
    if (iter == finish_networks_map_.end()) {
        finish_item = std::make_shared<BlsFinishItem>();
        finish_networks_map_[bls_msg.finish_req().network_id()] = finish_item;
    } else {
        finish_item = iter->second;
    }

    if (finish_item->verified[bls_msg.index()]) {
        return;
    }

    finish_item->verified[bls_msg.index()] = true;
    auto common_pk_iter = finish_item->common_pk_map.find(msg_hash);
    if (common_pk_iter == finish_item->common_pk_map.end()) {
        finish_item->common_pk_map[cpk_hash] = *common_pkey.getPublicKey();
    }

    finish_item->all_public_keys[bls_msg.index()] = *pkey.getPublicKey();
    finish_item->all_bls_signs[bls_msg.index()] = sign;
    finish_item->all_common_public_keys[bls_msg.index()] = *common_pkey.getPublicKey();
    auto cpk_iter = finish_item->max_public_pk_map.find(cpk_hash);
    if (cpk_iter == finish_item->max_public_pk_map.end()) {
        finish_item->max_public_pk_map[cpk_hash] = 1;
    } else {
        ++cpk_iter->second;
        common_pkey.getPublicKey()->to_affine_coordinates();
        if (cpk_iter->second >= t) {
            CheckAggSignValid(
                t,
                members->size(),
                *common_pkey.getPublicKey(),
                finish_item,
                bls_msg.index());
        }
    }

    if (finish_item->success_verified) {
        BLS_DEBUG("success check all members agg signature, elect_height: %lu", bls_msg.elect_height());
    }

    auto max_iter = finish_item->max_bls_members.find(msg_hash);
    if (max_iter != finish_item->max_bls_members.end()) {
        ++max_iter->second->count;
        if (max_iter->second->count > finish_item->max_finish_count) {
            finish_item->max_finish_count = max_iter->second->count;
            finish_item->max_finish_hash = msg_hash;
        }

        return;
    }

    std::vector<uint64_t> bitmap_data;
    for (int32_t i = 0; i < bls_msg.finish_req().bitmap_size(); ++i) {
        bitmap_data.push_back(bls_msg.finish_req().bitmap(i));
    }

    common::Bitmap bitmap(bitmap_data);
    auto item = std::make_shared<MaxBlsMemberItem>(1, bitmap);
    finish_item->max_bls_members[msg_hash] = item;
    if (finish_item->max_finish_count == 0) {
        finish_item->max_finish_count = 1;
        finish_item->max_finish_hash = msg_hash;
    }
}

void BlsManager::CheckAggSignValid(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        uint32_t member_idx) {
    if (finish_item->success_verified) {
        std::vector<libff::alt_bn128_G1> tmp_all_signs = finish_item->verified_valid_signs;
        std::vector<size_t> tmp_idx_vec = finish_item->verified_valid_index;
        tmp_all_signs[member_idx] = finish_item->all_bls_signs[member_idx];
        tmp_idx_vec[member_idx] = member_idx + 1;
        uint32_t tmp_idx = member_idx + 1;
        while (true) {
            if (tmp_idx >= n) {
                tmp_idx = 0;
            }

            if (tmp_idx_vec[tmp_idx] != 0) {
                tmp_idx_vec[tmp_idx] = 0;
                tmp_all_signs[tmp_idx] = libff::alt_bn128_G1::zero();
                break;
            }

            ++tmp_idx;
        }

        if (!VerifyAggSignValid(
                t,
                n,
                common_pk,
                finish_item,
                tmp_all_signs,
                tmp_idx_vec)) {
            finish_item->all_common_public_keys[member_idx] = libff::alt_bn128_G2::zero();
            BLS_ERROR("invalid bls item index: %d", member_idx);
        } else {
            BLS_ERROR("valid bls item index: %d", member_idx);
        }

        return;
    }

    // choose finished item t to verify
    std::vector<libff::alt_bn128_G1> all_signs(n, libff::alt_bn128_G1::zero());
    std::vector<size_t> idx_vec(n, 0);
    uint32_t start_pos = rand() % n;
    uint32_t i = start_pos;
    uint32_t valid_count = 0;
    uint32_t min_pos = common::kInvalidUint32;
    while (true) {
        if (i >= n) {
            i = 0;
            if (i == start_pos) {
                break;
            }
        }

        if (finish_item->all_common_public_keys[i] == libff::alt_bn128_G2::zero()) {
            ++i;
            continue;
        }

        if (finish_item->all_common_public_keys[i] != common_pk) {
            finish_item->all_common_public_keys[i].to_affine_coordinates();
            ++i;
            continue;
        }

        all_signs[i] = finish_item->all_bls_signs[i];
        idx_vec[i] = i + 1;
        ++valid_count;
        if (valid_count >= t) {
            break;
        }

        ++i;
        if (i == start_pos) {
            break;
        }
    }

    if (valid_count < t) {
        BLS_ERROR("valid_count: %d < t: %d", valid_count, t);
        return;
    }

    if (CheckAndVerifyAll(
            t,
            n,
            common_pk,
            finish_item,
            all_signs,
            idx_vec)) {
        return;
    }

    for (uint32_t i = 0; i < n; ++i) {
        if (finish_item->all_bls_signs[i] == libff::alt_bn128_G1::zero() ||
                finish_item->all_public_keys[i] == libff::alt_bn128_G2::zero() ||
                idx_vec[i] != 0) {
            continue;
        }

        uint32_t rand_weed_pos = rand() % n;
        uint32_t idx = rand_weed_pos + 1;
        while (true) {
            if (idx >= n) {
                idx = 0;
            }

            if (idx_vec[idx] != 0) {
                idx_vec[idx] = 0;
                all_signs[idx] = libff::alt_bn128_G1::zero();
                break;
            }

            if (idx == rand_weed_pos) {
                assert(false);
                break;
            }

            ++idx;
        }

        idx_vec[i] = i + 1;
        all_signs[i] = finish_item->all_bls_signs[i];
        if (CheckAndVerifyAll(
                t,
                n,
                common_pk,
                finish_item,
                all_signs,
                idx_vec)) {
            break;
        }
    }
}

bool BlsManager::CheckAndVerifyAll(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        std::vector<libff::alt_bn128_G1>& all_signs,
        std::vector<size_t>& idx_vec) {
    if (VerifyAggSignValid(
            t,
            n,
            common_pk,
            finish_item,
            all_signs,
            idx_vec)) {
        finish_item->success_verified = true;
        finish_item->verified_valid_signs = all_signs;
        finish_item->verified_valid_index = idx_vec;
        // verify all left members
        for (uint32_t member_idx = 0; member_idx < n; ++member_idx) {
            if (finish_item->all_bls_signs[member_idx] == libff::alt_bn128_G1::zero() ||
                    finish_item->all_public_keys[member_idx] == libff::alt_bn128_G2::zero() ||
                    finish_item->verified_valid_index[member_idx] != 0) {
                continue;
            }

            std::vector<libff::alt_bn128_G1> tmp_all_signs = finish_item->verified_valid_signs;
            std::vector<size_t> tmp_idx_vec = finish_item->verified_valid_index;
            tmp_all_signs[member_idx] = finish_item->all_bls_signs[member_idx];
            tmp_idx_vec[member_idx] = member_idx + 1;
            uint32_t tmp_idx = member_idx + 1;
            while (true) {
                if (tmp_idx >= n) {
                    tmp_idx = 0;
                }

                if (tmp_idx_vec[tmp_idx] != 0) {
                    assert(tmp_idx != member_idx);
                    tmp_idx_vec[tmp_idx] = 0;
                    tmp_all_signs[tmp_idx] = libff::alt_bn128_G1::zero();
                    break;
                }

                ++tmp_idx;
            }

            if (!VerifyAggSignValid(
                    t,
                    n,
                    common_pk,
                    finish_item,
                    tmp_all_signs,
                    tmp_idx_vec)) {
                finish_item->all_common_public_keys[member_idx] = libff::alt_bn128_G2::zero();
                finish_item->all_public_keys[member_idx] == libff::alt_bn128_G2::zero();
            }
        }

        return true;
    }

    return false;
}

bool BlsManager::VerifyAggSignValid(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& common_pk,
        BlsFinishItemPtr& finish_item,
        std::vector<libff::alt_bn128_G1>& in_all_signs,
        std::vector<size_t>& in_idx_vec) {
    std::vector<libff::alt_bn128_G1> all_signs;
    std::vector<size_t> idx_vec;
    for (uint32_t i = 0; i < in_idx_vec.size(); ++i) {
        if (in_idx_vec[i] != 0) {
            idx_vec.push_back(in_idx_vec[i]);
            all_signs.push_back(in_all_signs[i]);
        }
    }

    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
        auto bls_agg_sign = std::make_shared<libff::alt_bn128_G1>(bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
        std::string verify_hash;
        if (Verify(
                t,
                n,
                common_pk,
                *bls_agg_sign,
                finish_item->max_finish_hash,
                &verify_hash) != bls::kBlsSuccess) {
            ZJC_ERROR("verify agg sign failed!");
            return false;
        }

        return true;
    } catch (...) {
    }

    return false;
}

// int BlsManager::AddBlsConsensusInfo(
//         elect::protobuf::ElectBlock& ec_block,
//         common::Bitmap* bitmap) {
//     std::lock_guard<std::mutex> guard(finish_networks_map_mutex_);
//     auto iter = finish_networks_map_.find(ec_block.shard_network_id());
//     if (iter == finish_networks_map_.end()) {
//         BLS_ERROR("find finish_networks_map_ failed![%u]", ec_block.shard_network_id());
//         return kBlsError;
//     }
// 
//     if (!iter->second->success_verified) {
//         BLS_ERROR("success_verified failed![%u]", ec_block.shard_network_id());
//         return kBlsError;
//     }
// 
//     auto members = elect::ElectManager::Instance()->GetWaitingNetworkMembers(
//         ec_block.shard_network_id());
//     if (members == nullptr) {
//         BLS_ERROR("get waiting members failed![%u]", ec_block.shard_network_id());
//         return kBlsError;
//     }
// 
//     // At least so many nodes are required to successfully exchange keys
//     auto exchange_member_count = (uint32_t)((float)members->size() * kBlsMaxExchangeMembersRatio);
//     if (exchange_member_count < members->size()) {
//         ++exchange_member_count;
//     }
// 
//     auto t = common::GetSignerCount(members->size());
//     BlsFinishItemPtr finish_item = iter->second;
//     if (finish_item->max_finish_count < exchange_member_count) {
//         BLS_ERROR("network: %u, finish_item->max_finish_count < t[%u][%u]",
//             ec_block.shard_network_id(),
//             finish_item->max_finish_count, exchange_member_count);
//         return kBlsError;
//     }
// 
//     auto item_iter = finish_item->max_bls_members.find(finish_item->max_finish_hash);
//     if (item_iter == finish_item->max_bls_members.end()) {
//         BLS_ERROR("finish_item->max_bls_members failed");
//         return kBlsError;
//     }
// 
//     uint32_t max_mem_size = item_iter->second->bitmap.data().size() * 64;
//     if (max_mem_size < members->size()) {
//         BLS_ERROR("max_mem_size < members->size()[%u][%u]", max_mem_size, members->size());
//         return kBlsError;
//     }
// 
//     uint32_t max_cpk_count = 0;
//     std::string max_cpk_hash;
//     for (auto max_cpk_count_iter = finish_item->max_public_pk_map.begin();
//         max_cpk_count_iter != finish_item->max_public_pk_map.end(); ++max_cpk_count_iter) {
//         if (max_cpk_count_iter->second > max_cpk_count) {
//             max_cpk_count = max_cpk_count_iter->second;
//             max_cpk_hash = max_cpk_count_iter->first;
//         }
//     }
// 
//     auto common_pk_iter = finish_item->common_pk_map.find(max_cpk_hash);
//     if (common_pk_iter == finish_item->common_pk_map.end()) {
//         BLS_ERROR("finish_item->common_pk_map failed!");
//         return kBlsError;
//     }
// 
//     *bitmap = common::Bitmap(item_iter->second->bitmap.data().size() * 64);
//     auto pre_ec_members = ec_block.mutable_prev_members();
//     for (size_t i = 0; i < members->size(); ++i) {
//         auto mem_bls_pk = pre_ec_members->add_bls_pubkey();
//         if (!item_iter->second->bitmap.Valid(i)) {
//             mem_bls_pk->set_x_c0("");
//             mem_bls_pk->set_x_c1("");
//             mem_bls_pk->set_y_c0("");
//             mem_bls_pk->set_y_c1("");
//             BLS_WARN("0 invalid bitmap: %d", i);
//             continue;
//         }
// 
//         if (finish_item->all_public_keys[i] == libff::alt_bn128_G2::zero()) {
//             mem_bls_pk->set_x_c0("");
//             mem_bls_pk->set_x_c1("");
//             mem_bls_pk->set_y_c0("");
//             mem_bls_pk->set_y_c1("");
//             BLS_WARN("0 invalid all_public_keys: %d", i);
//             continue;
//         }
// 
//         if (finish_item->all_common_public_keys[i] != common_pk_iter->second) {
//             mem_bls_pk->set_x_c0("");
//             mem_bls_pk->set_x_c1("");
//             mem_bls_pk->set_y_c0("");
//             mem_bls_pk->set_y_c1("");
//             BLS_WARN("0 invalid all_common_public_keys: %d", i);
//             continue;
//         }
// 
//         finish_item->all_public_keys[i].to_affine_coordinates();
//         mem_bls_pk->set_x_c0(
//             libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].X.c0));
//         mem_bls_pk->set_x_c1(
//             libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].X.c1));
//         mem_bls_pk->set_y_c0(
//             libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].Y.c0));
//         mem_bls_pk->set_y_c1(
//             libBLS::ThresholdUtils::fieldElementToString(finish_item->all_public_keys[i].Y.c1));
//         mem_bls_pk->set_pool_idx_mod_num(-1);
//         bitmap->Set(i);
//     }
// 
//     if (bitmap->valid_count() < exchange_member_count) {
//         ec_block.clear_prev_members();
//         BLS_ERROR("all_valid_count < t[%u][%u][%f][%u]",
//             bitmap->valid_count(),
//             members->size(),
//             kBlsMaxExchangeMembersRatio,
//             exchange_member_count);
//         return kBlsError;
//     }
// 
//     common_pk_iter->second.to_affine_coordinates();
//     auto common_pk = pre_ec_members->mutable_common_pubkey();
//     common_pk->set_x_c0(
//         libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.X.c0));
//     common_pk->set_x_c1(
//         libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.X.c1));
//     common_pk->set_y_c0(
//         libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.Y.c0));
//     common_pk->set_y_c1(
//         libBLS::ThresholdUtils::fieldElementToString(common_pk_iter->second.Y.c1));
//     pre_ec_members->set_prev_elect_height(
//         elect::ElectManager::Instance()->waiting_elect_height(ec_block.shard_network_id()));
//     BLS_DEBUG("network: %u, AddBlsConsensusInfo success max_finish_count_: %d,"
//         "member count: %d, x_c0: %s, x_c1: %s, y_c0: %s, y_c1: %s.",
//         ec_block.shard_network_id(),
//         bitmap->valid_count(), members->size(),
//         common_pk->x_c0().c_str(), common_pk->x_c1().c_str(),
//         common_pk->y_c0().c_str(), common_pk->y_c1().c_str());
//     return kBlsSuccess;
// }

BlsManager::BlsManager(
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<db::Db>& db) : security_(security), db_(db) {
    initLibSnark();
    network::Route::Instance()->RegisterMessage(
        common::kBlsMessage,
        std::bind(&BlsManager::HandleMessage, this, std::placeholders::_1));
}

BlsManager::~BlsManager() {}

};  // namespace bls

};  // namespace zjchain
