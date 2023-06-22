#include "bls/bls_dkg.h"

#include <vector>
#include <fstream>

#include <libbls/tools/utils.h>
#include <dkg/dkg.h>

#include "bls/bls_utils.h"
#include "bls/bls_manager.h"
#include "common/global_info.h"
#include "db/db.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "network/network_utils.h"
#include "protos/get_proto_hash.h"
#include "protos/prefix_db.h"
#include "json/json.hpp"

namespace zjchain {

namespace bls {

BlsDkg::BlsDkg() {}

BlsDkg::~BlsDkg() {}

void BlsDkg::Init(
        BlsManager* bls_mgr,
        std::shared_ptr<security::Security>& security,
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& local_sec_key,
        const libff::alt_bn128_G2 local_publick_key,
        const libff::alt_bn128_G2 common_public_key,
        std::shared_ptr<db::Db>& db) {
    bls_mgr_ = bls_mgr;
    security_ = security;
    local_sec_key_ = local_sec_key;
    local_publick_key_ = local_publick_key;
    common_public_key_ = common_public_key;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
}

void BlsDkg::Destroy() {
}

void BlsDkg::TimerMessage(uint8_t thread_idx) {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    PopBlsMessage(thread_idx);
    if (!has_broadcast_verify_ &&
            now_tm_us < (begin_time_us_ + kDkgPeriodUs * 4) &&
            now_tm_us > (begin_time_us_ + ver_offset_)) {
        ZJC_DEBUG("now call send verify g2.");
        BroadcastVerfify(thread_idx);
        has_broadcast_verify_ = true;
    }

    if (has_broadcast_verify_ && !has_broadcast_swapkey_ && 
            now_tm_us < (begin_time_us_ + kDkgPeriodUs * 7) &&
            now_tm_us >(begin_time_us_ + swap_offset_)) {
        ZJC_DEBUG("now call send swap sec key.");
        SwapSecKey(thread_idx);
        has_broadcast_swapkey_ = true;
    }

    if (has_broadcast_swapkey_ && !has_finished_ &&
            now_tm_us < (begin_time_us_ + kDkgPeriodUs * 10) &&
            now_tm_us >(begin_time_us_ + finish_offset_)) {
        ZJC_DEBUG("now call send finish.");
        FinishBroadcast(thread_idx);
        has_finished_ = true;
    }
}

void BlsDkg::OnNewElectionBlock(
        uint64_t elect_height,
        common::MembersPtr& members,
        std::shared_ptr<TimeBlockItem>& latest_timeblock_info) try {
    if (elect_height <= elect_hegiht_) {
        return;
    }

    memset(valid_swaped_keys_, 0, sizeof(valid_swaped_keys_));
    memset(has_swaped_keys_, 0, sizeof(has_swaped_keys_));
    finished_ = false;
    // destroy old timer
    max_finish_count_ = 0;
    max_finish_hash_ = "";
    valid_sec_key_count_ = 0;
    members_ = members;
    valid_swapkey_set_.clear();
    member_count_ = members_->size();
    for_common_pk_g2s_ = std::vector<libff::alt_bn128_G2>(member_count_, libff::alt_bn128_G2::zero());
    min_aggree_member_count_ = common::GetSignerCount(member_count_);
    elect_hegiht_ = elect_height;
    for (uint32_t i = 0; i < member_count_; ++i) {
        if ((*members_)[i]->id == security_->GetAddress()) {
            local_member_index_ = i;
            prefix_db_->SaveLocalElectPos(security_->GetAddress(), local_member_index_);
            break;
        }
    }

    auto tmblock_tm = (latest_timeblock_info->lastest_time_block_tm +
        common::kTimeBlockCreatePeriodSeconds - kTimeBlsPeriodSeconds) * 1000l * 1000l;
    uint64_t end_tm_point = (latest_timeblock_info->lastest_time_block_tm +
        common::kTimeBlockCreatePeriodSeconds) * 1000000lu;
    begin_time_us_ = common::TimeUtils::TimestampUs();
    ver_offset_ = 0;
    swap_offset_ = kDkgPeriodUs * 4;
    finish_offset_ = kDkgPeriodUs * 7;
    auto bls_period = kTimeBlsPeriodSeconds * 1000l * 1000l;
    if (begin_time_us_ < tmblock_tm && end_tm_point > tmblock_tm) {
        kDkgPeriodUs = (end_tm_point - tmblock_tm) / 10l;
        begin_time_us_ = tmblock_tm;
        ver_offset_ = 0;
        swap_offset_ = kDkgPeriodUs * 4;
        finish_offset_ = kDkgPeriodUs * 7;
    }

    ver_offset_ += (common::Random::RandomUint32() % (kDkgPeriodUs * 3 / 1000000lu)) * 1000000lu;
    swap_offset_ += (common::Random::RandomUint32() % (kDkgPeriodUs * 3 / 1000000lu)) * 1000000lu;
    finish_offset_ += (common::Random::RandomUint32() % (kDkgPeriodUs * 3 / 1000000lu)) * 1000000lu;
    ZJC_DEBUG("bls time point now: %u, time block tm: %u, begin_time_sec_: %u, "
        "kDkgPeriodUs: %u, ver_offset_: %u, swap_offset_: %u, finish_offset_: %u",
        common::TimeUtils::TimestampSeconds(),
        latest_timeblock_info->lastest_time_block_tm,
        begin_time_us_ / 1000000,
        kDkgPeriodUs / 1000000,
        ver_offset_ / 1000000,
        swap_offset_ / 1000000,
        finish_offset_ / 1000000);
    has_broadcast_verify_ = false;
    has_broadcast_swapkey_ = false;
    has_finished_ = false;
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::HandleMessage(const transport::MessagePtr& msg_ptr) {
    bls_msg_queue_.push(msg_ptr);
}

void BlsDkg::PopBlsMessage(uint8_t thread_idx) {
    while (bls_msg_queue_.size() > 0) {
        transport::MessagePtr msg_ptr = nullptr;
        if (!bls_msg_queue_.pop(&msg_ptr)) {
            break;
        }

        msg_ptr->thread_idx = thread_idx;
        HandleBlsMessage(msg_ptr);
    }
}

void BlsDkg::HandleBlsMessage(const transport::MessagePtr& msg_ptr) try {
    if (members_ == nullptr) {
        BLS_ERROR("members_ == nullptr");
        return;
    }

    if (local_member_index_ == common::kInvalidUint32) {
        BLS_INFO("bls create HandleSwapSecKey block elect_height: %lu", elect_hegiht_);
        return;
    }

    auto& header = msg_ptr->header;
    assert(header.type() == common::kBlsMessage);
    // must verify message signature, to avoid evil node
    auto& bls_msg = header.bls_proto();
    if (member_count_ <= bls_msg.index()) {
        BLS_ERROR("member_count_ <= bls_msg.index(): %d, %d",
            member_count_, bls_msg.index());
        return;
    }

    if (bls_msg.index() >= member_count_) {
        BLS_ERROR("bls_msg.index() >= member_count_");
        return;
    }

    if (bls_msg.elect_height() == 0 || bls_msg.elect_height() != elect_hegiht_) {
        BLS_ERROR("bls_msg.elect_height() != elect_height: %lu, %lu",
            bls_msg.elect_height(), elect_hegiht_);
        return;
    }

    if (bls_msg.has_verify_brd()) {
        HandleVerifyBroadcast(msg_ptr);
    }

    if (bls_msg.has_swap_req()) {
        HandleSwapSecKey(msg_ptr);
    }

    if (bls_msg.has_check_verify_req()) {
        HandleCheckVerifyReq(msg_ptr);
    }

    if (bls_msg.has_check_swapkey_req()) {
        HandleCheckSwapKeyReq(msg_ptr);
    }
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

bool BlsDkg::IsSignValid(const transport::MessagePtr& msg_ptr, std::string* content_to_hash) {
#ifdef ZJC_UNITTEST
    return true;
#endif // ZJC_UNITTEST
    protos::GetProtoHash(msg_ptr->header, content_to_hash);
    auto& pubkey = (*members_)[msg_ptr->header.bls_proto().index()]->pubkey;
    if (security_->Verify(
            *content_to_hash,
            pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        BLS_INFO("bls create IsSignValid error block elect_height: %lu", elect_hegiht_);
        return false;
    }

    return true;
}

void BlsDkg::HandleVerifyBroadcast(const transport::MessagePtr& msg_ptr) try {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (member_count_ <= bls_msg.index()) {
        BLS_ERROR("member_count_ <= bls_msg.index(), %u : %u",
            member_count_, bls_msg.index());
        assert(false);
        return;
    }

//     if (prefix_db_->ExistsBlsVerifyG2((*members_)[bls_msg.index()]->id)) {
//         assert(false);
//         return;
//     }

    if (!IsVerifyBrdPeriod()) {
//         assert(false);
        return;
    }

    std::string msg_hash;
    if (!IsSignValid(msg_ptr, &msg_hash)) {
        BLS_ERROR("sign verify failed!");
        return;
    }

    if (1 != (uint32_t)bls_msg.verify_brd().verify_vec_size()) {
        BLS_ERROR("min_aggree_member_count_ != "
            "bls_msg.verify_brd().verify_vec_size()[%d: %d]",
            1,
            bls_msg.verify_brd().verify_vec_size());
//         assert(false);
        return;
    }

    prefix_db_->AddBlsVerifyG2((*members_)[bls_msg.index()]->id, bls_msg.verify_brd());
    ZJC_DEBUG("save verify g2 success local: %d, %lu, %u, %u, %s, %s",
        local_member_index_, elect_hegiht_, bls_msg.index(), 0,
        common::Encode::HexEncode((*members_)[bls_msg.index()]->id).c_str(),
        common::Encode::HexEncode(bls_msg.verify_brd().verify_vec(0).x_c0()).c_str());
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::CheckVerifyAllValid(uint8_t thread_idx) {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    if (now_tm_us > (begin_time_us_ + kDkgPeriodUs * 2 + 5) &&
            now_tm_us < (begin_time_us_ + kDkgPeriodUs * 4 - 5)) {
        for (uint32_t i = 0; i < member_count_; ++i) {
            if (i == local_member_index_) {
                continue;
            }

            auto iter = verify_map_.find(i);
            if (iter == verify_map_.end()) {
                SendGetVerifyInfo(thread_idx, i);
            }
        }
    }
// 
//     if (now_tm_us < (begin_time_us_ + kDkgPeriodUs * 4 - 5)) {
//         check_verify_brd_timer_.CutOff(
//             3000000l,
//             std::bind(&BlsDkg::CheckVerifyAllValid, this, std::placeholders::_1));
//     }
}

void BlsDkg::SendGetVerifyInfo(uint8_t thread_idx, int32_t index) {
    auto dht = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    bls_msg.set_index(local_member_index_);
    bls_msg.set_elect_height(elect_hegiht_);
    auto check_verify_req = bls_msg.mutable_check_verify_req();
    check_verify_req->set_index(index);
    msg.set_src_sharding_id(dht->local_node()->sharding_id);
    msg.set_des_dht_key(dht->local_node()->dht_key);
    msg.set_type(common::kBlsMessage);
    dht->RandomSend(msg_ptr);
    BLS_DEBUG("send get verify req elect_height: %lu, index: %d", elect_hegiht_, index);
}

void BlsDkg::CheckSwapKeyAllValid(uint8_t thread_idx) {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    if (now_tm_us > (begin_time_us_ + kDkgPeriodUs * 4 + 5) &&
            now_tm_us < (begin_time_us_ + kDkgPeriodUs * 8 - 5)) {
        for (uint32_t i = 0; i < member_count_; ++i) {
            auto iter = valid_swapkey_set_.find(i);
            if (iter == valid_swapkey_set_.end()) {
                SendGetSwapKey(thread_idx, i);
            }
        }
    }

//     if (now_tm_us < (begin_time_us_ + kDkgPeriodUs * 8 - 5)) {
//         check_swap_seckkey_timer_.CutOff(
//             3000000l,
//             std::bind(&BlsDkg::CheckSwapKeyAllValid, this, std::placeholders::_1));
//     }
}

void BlsDkg::SendGetSwapKey(uint8_t thread_idx, int32_t index) {
    auto dht = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    bls_msg.set_index(local_member_index_);
    bls_msg.set_elect_height(elect_hegiht_);
    auto check_swapkey_req = bls_msg.mutable_check_swapkey_req();
    check_swapkey_req->set_index(index);
    msg.set_src_sharding_id(dht->local_node()->sharding_id);
    msg.set_des_dht_key(dht->local_node()->dht_key);
    msg.set_type(common::kBlsMessage);
    dht->RandomSend(msg_ptr);
    BLS_DEBUG("send get swap key req elect_height: %lu, index: %d", elect_hegiht_, index);
}

void BlsDkg::HandleCheckVerifyReq(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    auto iter = verify_map_.find(bls_msg.check_verify_req().index());
    if (iter == verify_map_.end()) {
        return;
    }

    transport::protobuf::Header msg;
    auto& res_bls_msg = *msg.mutable_bls_proto();
    res_bls_msg.ParseFromString(iter->second);
    msg.set_src_sharding_id(dht::DhtKeyManager::DhtKeyGetNetId(header.des_dht_key()));
    msg.set_des_dht_key(header.des_dht_key());
    msg.set_type(common::kBlsMessage);
    msg_ptr->conn->Send(msg.SerializeAsString());
}

void BlsDkg::HandleCheckSwapKeyReq(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    auto iter = swap_key_map_.find(bls_msg.check_swapkey_req().index());
    if (iter == swap_key_map_.end()) {
        return;
    }

    transport::protobuf::Header msg;
    auto& res_bls_msg = *msg.mutable_bls_proto();
    res_bls_msg.ParseFromString(iter->second);
    msg.set_src_sharding_id(dht::DhtKeyManager::DhtKeyGetNetId(header.des_dht_key()));
    msg.set_des_dht_key(header.des_dht_key());
    msg.set_type(common::kBlsMessage);
    msg_ptr->conn->Send(msg.SerializeAsString());
}

void BlsDkg::HandleSwapSecKey(const transport::MessagePtr& msg_ptr) try {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (!IsSwapKeyPeriod()) {
        //assert(false);
        return;
    }

    if (bls_msg.swap_req().keys_size() <= (int32_t)local_member_index_) {
        ZJC_WARN("swap key size error: %d, %d",
            bls_msg.swap_req().keys_size(), local_member_index_);
        return;
    }

    std::string msg_hash;
    if (!IsSignValid(msg_ptr, &msg_hash)) {
        BLS_ERROR("sign verify failed!");
        return;
    }

    std::string dec_msg;
    std::string encrypt_key;
    if (security_->GetEcdhKey(
            (*members_)[bls_msg.index()]->pubkey,
            &encrypt_key) != security::kSecuritySuccess) {
        BLS_WARN("invalid ecdh key.");
        return;
    }

    int res = security_->Decrypt(
        bls_msg.swap_req().keys(local_member_index_).sec_key(),
        encrypt_key,
        &dec_msg);
    if (dec_msg.empty()) {
        BLS_ERROR("dec_msg.empty()");
        return;
    }

    std::string sec_key(dec_msg.substr(
        0,
        bls_msg.swap_req().keys(local_member_index_).sec_key_len()));
    if (!IsValidBigInt(sec_key)) {
        BLS_ERROR("invalid big int[%s]", sec_key.c_str());
        assert(false);
        return;
    }

    if (has_swaped_keys_[bls_msg.index()]) {
        BLS_WARN("has_swaped_keys_  %d.", bls_msg.index());
        return;
    }

    auto tmp_swap_key = libff::alt_bn128_Fr(sec_key.c_str());
    if (!VerifySekkeyValid(bls_msg.index(), tmp_swap_key)) {
        ZJC_ERROR("verify error member: %d, index: %d, %s , min_aggree_member_count_: %d",
            local_member_index_, bls_msg.index(),
            libBLS::ThresholdUtils::fieldElementToString(tmp_swap_key).c_str(),
            min_aggree_member_count_);
//         assert(false);
        return;
    }

    ZJC_DEBUG("swap verify success member: %d, index: %d, %s, min_aggree_member_count_: %u",
        local_member_index_, bls_msg.index(),
        libBLS::ThresholdUtils::fieldElementToString(tmp_swap_key).c_str(),
        min_aggree_member_count_);
    // swap
    prefix_db_->SaveSwapKey(
        local_member_index_, elect_hegiht_, local_member_index_, bls_msg.index(), sec_key);
    valid_swapkey_set_.insert(bls_msg.index());
    ++valid_sec_key_count_;
    has_swaped_keys_[bls_msg.index()] = true;
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

bool BlsDkg::VerifySekkeyValid(
        uint32_t peer_index,
        const libff::alt_bn128_Fr& seckey) {
    bls::protobuf::BlsVerifyValue verify_val;
    uint32_t changed_idx = 0;
    libff::alt_bn128_G2 new_val = GetVerifyG2FromDb(peer_index, &changed_idx);
    if (new_val == libff::alt_bn128_G2::zero()) {
        assert(false);
        return false;
    }

    bls::protobuf::JoinElectBlsInfo verfy_final_vals;
    if (!prefix_db_->GetVerifiedG2s(
            local_member_index_,
            (*members_)[peer_index]->id,
            min_aggree_member_count_,
            &verfy_final_vals)) {
        // compute verified g2s with new index
        if (!CheckRecomputeG2s((*members_)[peer_index]->id, verfy_final_vals)) {
            ZJC_WARN("failed get verified g2: %u, %s",
                local_member_index_,
                common::Encode::HexEncode((*members_)[peer_index]->id).c_str());
//             assert(false);
            return false;
        }
    }

    bls::protobuf::JoinElectInfo join_info;
    if (!prefix_db_->GetNodeVerificationVector((*members_)[peer_index]->id, &join_info)) {
        assert(false);
        return false;
    }

    if (join_info.g2_req().verify_vec_size() <= (int32_t)changed_idx) {
        assert(false);
        return false;
    }

    libff::alt_bn128_G2 old_val;
    {
        auto& item = join_info.g2_req().verify_vec(changed_idx);
        auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
        auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
        auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
        auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
        auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
        auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
        auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
        auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
        auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
        old_val = libff::alt_bn128_G2(x_coord, y_coord, z_coord);

        if (changed_idx == 0) {
            for_common_pk_g2s_[peer_index] = new_val;
        } else {
            auto& item = join_info.g2_req().verify_vec(0);
            auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
            auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
            auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
            auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
            auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
            auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
            auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
            auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
            auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
            auto tmp_val = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
            for_common_pk_g2s_[peer_index] = tmp_val;
        }
    }

    auto& item = verfy_final_vals.verified_g2();
    auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
    auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
    auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
    auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
    auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
    auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
    auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
    auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
    auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
    auto all_verified_val = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
    auto old_g2_val = power(libff::alt_bn128_Fr(local_member_index_ + 1), changed_idx) * old_val;
    auto new_g2_val = power(libff::alt_bn128_Fr(local_member_index_ + 1), changed_idx) * new_val;
    all_verified_val = all_verified_val - old_g2_val + new_g2_val;
    if (all_verified_val != seckey * libff::alt_bn128_G2::one()) {
        for_common_pk_g2s_[peer_index] = libff::alt_bn128_G2::zero();
        return false;
    }

    return true;
}

bool BlsDkg::CheckRecomputeG2s(
        const std::string& id,
        bls::protobuf::JoinElectBlsInfo& verfy_final_vals) {
    bls::protobuf::JoinElectInfo join_info;
    if (!prefix_db_->GetNodeVerificationVector(id, &join_info)) {
//         ZJC_DEBUG("failed get verifcaton handle kElectJoin tx: %s", common::Encode::HexEncode(id).c_str());
        return false;
    }

    int32_t min_idx = 0;
    if (join_info.g2_req().verify_vec_size() >= kVerifiedG2Offset) {
        min_idx = join_info.g2_req().verify_vec_size() - kVerifiedG2Offset;
    }

    libff::alt_bn128_G2 verify_g2s = libff::alt_bn128_G2::zero();
    int32_t begin_idx = min_aggree_member_count_ - 1;
    for (; begin_idx > min_idx; --begin_idx) {
        if (prefix_db_->GetVerifiedG2s(local_member_index_, id, begin_idx + 1, &verfy_final_vals)) {
            auto& item = verfy_final_vals.verified_g2();
            auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
            auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
            auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
            auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
            auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
            auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
            auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
            auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
            auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
            verify_g2s = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
            ++begin_idx;
            break;
        }
    }

    if (verify_g2s == libff::alt_bn128_G2::zero()) {
        begin_idx = 0;
    }

    for (uint32_t i = begin_idx; i < min_aggree_member_count_; ++i) {
        auto& item = join_info.g2_req().verify_vec(i);
        auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
        auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
        auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
        auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
        auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
        auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
        auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
        auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
        auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
        auto g2 = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
        verify_g2s = verify_g2s + power(libff::alt_bn128_Fr(local_member_index_ + 1), i) * g2;
        bls::protobuf::VerifyVecItem& verify_item = *verfy_final_vals.mutable_verified_g2();
        verify_item.set_x_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.X.c0)));
        verify_item.set_x_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.X.c1)));
        verify_item.set_y_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.Y.c0)));
        verify_item.set_y_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.Y.c1)));
        verify_item.set_z_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.Z.c0)));
        verify_item.set_z_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2s.Z.c1)));
        auto verified_val = verfy_final_vals.SerializeAsString();
        prefix_db_->SaveVerifiedG2s(local_member_index_, id, i + 1, verfy_final_vals);
//         ZJC_DEBUG("success save verified g2: %u, peer: %d, t: %d, %s, %s",
//             local_member_index_,
//             join_info.member_idx(),
//             i + 1,
//             common::Encode::HexEncode(id).c_str(),
//             libBLS::ThresholdUtils::fieldElementToString(verify_g2s.X.c0).c_str());
    }

    return true;
}

libff::alt_bn128_G2 BlsDkg::GetVerifyG2FromDb(uint32_t peer_mem_index, uint32_t* changed_idx) {
    bls::protobuf::VerifyVecBrdReq req;
    auto res = prefix_db_->GetBlsVerifyG2((*members_)[peer_mem_index]->id, &req);
    if (!res) {
        ZJC_DEBUG("get verify g2 failed local: %d, %lu, %u",
            local_member_index_, elect_hegiht_, peer_mem_index);
        return libff::alt_bn128_G2::zero();
    }

    auto& item = req.verify_vec(0);
    auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
    auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
    auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
    auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
    auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
    auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
    auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
    auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
    auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
    *changed_idx = req.change_idx();
    return libff::alt_bn128_G2(x_coord, y_coord, z_coord);
}

void BlsDkg::BroadcastVerfify(uint8_t thread_idx) try {
    if (members_ == nullptr || local_member_index_ >= member_count_) {
        ZJC_ERROR("member null or member index invalid!");
        assert(false);
        return;
    }

    CreateContribution(member_count_, min_aggree_member_count_);
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    auto* verfiy_brd = bls_msg.mutable_verify_brd();
    auto res = prefix_db_->GetBlsVerifyG2((*members_)[local_member_index_]->id, verfiy_brd);
    if (!res) {
        assert(false);
        return;
    }

    ZJC_DEBUG("brd verify g2 success local net: %u, local: %d,  %s, %s",
        common::GlobalInfo::Instance()->network_id(),
        local_member_index_,
        common::Encode::HexEncode((*members_)[local_member_index_]->id).c_str(),
        common::Encode::HexEncode(bls_msg.verify_brd().verify_vec(0).x_c0()).c_str());
    CreateDkgMessage(msg_ptr);
#ifdef ZJC_UNITTEST
    ver_brd_msg_ = msg_ptr;
#else
    network::Route::Instance()->Send(msg_ptr);
#endif
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::SwapSecKey(uint8_t thread_idx) try {
    if (members_ == nullptr || local_member_index_ >= member_count_) {
        ZJC_ERROR("members invalid!");
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    auto swap_req = bls_msg.mutable_swap_req();
    for (uint32_t i = 0; i < member_count_; ++i) {
        auto swap_item = swap_req->add_keys();
        swap_item->set_sec_key("");
        swap_item->set_sec_key_len(0);
        if (valid_swaped_keys_[i]) {
            BLS_DEBUG("valid_swaped_keys_: %d", i);
            continue;
        }

        if (i == local_member_index_) {
            continue;
        }

        std::string seckey;
        int32_t seckey_len = 0;
        CreateSwapKey(i, &seckey, &seckey_len);
        if (seckey_len == 0) {
            continue;
        }

        swap_item->set_sec_key(seckey);
        swap_item->set_sec_key_len(seckey_len);
    }

    CreateDkgMessage(msg_ptr);
    ZJC_DEBUG("success send swap seckey request local member index: %d, local net: %u",
        local_member_index_, common::GlobalInfo::Instance()->network_id());
#ifdef ZJC_UNITTEST
    sec_swap_msgs_ = msg_ptr;
    ZJC_DEBUG("success add swap msg");
#else
    network::Route::Instance()->Send(msg_ptr);
#endif
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::CreateSwapKey(uint32_t member_idx, std::string* seckey, int32_t* seckey_len) {
    if (members_ == nullptr || local_member_index_ >= member_count_) {
        return;
    }

    auto msg = libBLS::ThresholdUtils::fieldElementToString(
        local_src_secret_key_contribution_[member_idx]);
    std::string encrypt_key;
    if (security_->GetEcdhKey(
            (*members_)[member_idx]->pubkey,
            &encrypt_key) != security::kSecuritySuccess) {
        return;
    }

    int res = security_->Encrypt(
        msg,
        encrypt_key,
        seckey);
    if (res != security::kSecuritySuccess) {
        return;
    }

    *seckey_len = msg.size();
}

void BlsDkg::FinishBroadcast(uint8_t thread_idx) try {
    if (members_ == nullptr ||
            local_member_index_ >= member_count_ ||
            valid_sec_key_count_ < min_aggree_member_count_) {
        BLS_ERROR("elect_height: %lu, valid count error.valid_sec_key_count_: %d,"
            "min_aggree_member_count_: %d, members_ == nullptr: %d, local_member_index_: %d,"
            "member_count_: %d",
            elect_hegiht_, valid_sec_key_count_, min_aggree_member_count_,
            (members_ == nullptr), local_member_index_, member_count_);
        return;
    }

    uint32_t bitmap_size = member_count_ / 64 * 64;
    if (member_count_ % 64 > 0) {
        bitmap_size += 64;
    }

    common::Bitmap bitmap(bitmap_size);
    common_public_key_ = libff::alt_bn128_G2::zero();
    std::vector<libff::alt_bn128_Fr> valid_seck_keys;
    for (size_t i = 0; i < member_count_; ++i) {
        auto iter = valid_swapkey_set_.find(i);
        if (iter == valid_swapkey_set_.end()) {
            valid_seck_keys.push_back(libff::alt_bn128_Fr::zero());
            common_public_key_ = common_public_key_ + libff::alt_bn128_G2::zero();
            BLS_DEBUG("elect_height: %d, invalid swapkey index: %d", elect_hegiht_, i);
            continue;
        }

        if (for_common_pk_g2s_[i] == libff::alt_bn128_G2::zero()) {
            valid_seck_keys.push_back(libff::alt_bn128_Fr::zero());
            common_public_key_ = common_public_key_ + libff::alt_bn128_G2::zero();
            BLS_DEBUG("elect_height: %d, invalid all_verification_vector index: %d",
                elect_hegiht_, i);
            continue;
        }

        std::string seckey;
        if (!prefix_db_->GetSwapKey(
                local_member_index_,
                elect_hegiht_,
                local_member_index_,
                i,
                &seckey)) {
            valid_seck_keys.push_back(libff::alt_bn128_Fr::zero());
            common_public_key_ = common_public_key_ + libff::alt_bn128_G2::zero();
            BLS_DEBUG("elect_height: %d, invalid secret_key_contribution_ index: %d",
                elect_hegiht_, i);
            continue;
        }

        valid_seck_keys.push_back(libff::alt_bn128_Fr(seckey.c_str()));
        common_public_key_ = common_public_key_ + for_common_pk_g2s_[i];
        bitmap.Set(i);
    }

    uint32_t valid_count = static_cast<uint32_t>(
        (float)member_count_ * kBlsMaxExchangeMembersRatio);
    if (bitmap.valid_count() < valid_count) {
        BLS_ERROR("elect_height: %d, bitmap.valid_count: %u < %u, "
            "member_count_: %u, kBlsMaxExchangeMembersRatio: %f",
            elect_hegiht_, bitmap.valid_count(), valid_count,
            member_count_, kBlsMaxExchangeMembersRatio);
        return;
    }

    libBLS::Dkg dkg(min_aggree_member_count_, member_count_);
    local_sec_key_ = dkg.SecretKeyShareCreate(valid_seck_keys);
    local_publick_key_ = dkg.GetPublicKeyFromSecretKey(local_sec_key_);
    DumpLocalPrivateKey();
    BroadcastFinish(thread_idx, bitmap);
    finished_ = true;
} catch (std::exception& e) {
    local_sec_key_ = libff::alt_bn128_Fr::zero();
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::DumpLocalPrivateKey() {
    std::string enc_data;
    std::string sec_key = libBLS::ThresholdUtils::fieldElementToString(local_sec_key_);
    if (security_->Encrypt(
            sec_key,
            security_->GetPrikey(),
            &enc_data) != security::kSecuritySuccess) {
        return;
    }

    prefix_db_->SaveBlsPrikey(
        elect_hegiht_,
        common::GlobalInfo::Instance()->network_id(),
        security_->GetAddress(),
        enc_data);
}

void BlsDkg::BroadcastFinish(uint8_t thread_idx, const common::Bitmap& bitmap) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    auto finish_msg = bls_msg.mutable_finish_req();
    auto& data = bitmap.data();
    for (uint32_t i = 0; i < data.size(); ++i) {
        finish_msg->add_bitmap(data[i]);
    }

    local_publick_key_.to_affine_coordinates();
    auto local_pk = finish_msg->mutable_pubkey();
    local_pk->set_x_c0(
        libBLS::ThresholdUtils::fieldElementToString(local_publick_key_.X.c0));
    local_pk->set_x_c1(
        libBLS::ThresholdUtils::fieldElementToString(local_publick_key_.X.c1));
    local_pk->set_y_c0(
        libBLS::ThresholdUtils::fieldElementToString(local_publick_key_.Y.c0));
    local_pk->set_y_c1(
        libBLS::ThresholdUtils::fieldElementToString(local_publick_key_.Y.c1));
    finish_msg->set_network_id(common::GlobalInfo::Instance()->network_id());
    auto common_pk = finish_msg->mutable_common_pubkey();
    common_public_key_.to_affine_coordinates();
    common_pk->set_x_c0(
        libBLS::ThresholdUtils::fieldElementToString(common_public_key_.X.c0));
    common_pk->set_x_c1(
        libBLS::ThresholdUtils::fieldElementToString(common_public_key_.X.c1));
    common_pk->set_y_c0(
        libBLS::ThresholdUtils::fieldElementToString(common_public_key_.Y.c0));
    common_pk->set_y_c1(
        libBLS::ThresholdUtils::fieldElementToString(common_public_key_.Y.c1));
    std::string pk = common_pk->x_c0() + common_pk->x_c1() + common_pk->y_c0() + common_pk->y_c1();
    std::string sign_x;
    std::string sign_y;
    libff::alt_bn128_G1 g1_hash;
    CreateDkgMessage(msg_ptr);
    std::string sign_hash = common::Hash::keccak256(pk);
    bls_mgr_->GetLibffHash(sign_hash, &g1_hash);
    if (bls_mgr_->Sign(
            min_aggree_member_count_,
            member_count_,
            local_sec_key_,
            g1_hash,
            &sign_x,
            &sign_y) != kBlsSuccess) {
        ZJC_FATAL("sign bls message failed!");
        return;
    }

    finish_msg->set_bls_sign_x(sign_x);
    finish_msg->set_bls_sign_y(sign_y);
#ifndef ZJC_UNITTEST
    ZJC_DEBUG("success broadcast finish message. t: %d, n: %d, msg hash: %s, pk: %s",
        min_aggree_member_count_, member_count_,
        common::Encode::HexEncode(sign_hash).c_str(), pk.c_str());
    network::Route::Instance()->Send(msg_ptr);
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        bls_mgr_->HandleMessage(msg_ptr);
    }
#endif
}

void BlsDkg::CreateContribution(uint32_t valid_n, uint32_t valid_t) {
    bls::protobuf::LocalPolynomial local_poly;
    if (!prefix_db_->GetLocalPolynomial(security_, security_->GetAddress(), &local_poly)) {
        assert(false);
        return;
    }

    if (local_poly.polynomial_size() < (int32_t)valid_t) {
        assert(false);
        return;
    }

    std::vector<libff::alt_bn128_Fr> polynomial(valid_t);
    int32_t change_idx = common::Random::RandomInt32() % valid_t;
    libff::alt_bn128_G2 old_g2 = libff::alt_bn128_G2::zero();
    for (uint32_t i = 0; i < valid_t; ++i) {
        polynomial[i] = libff::alt_bn128_Fr(common::Encode::HexEncode(local_poly.polynomial(i)).c_str());
        if (change_idx == (int32_t)i) {
            old_g2 = polynomial[i] * libff::alt_bn128_G2::one();
            polynomial[i] = libff::alt_bn128_Fr::random_element();
            while (polynomial[i] == libff::alt_bn128_Fr::zero()) {
                polynomial[i] = libff::alt_bn128_Fr::random_element();
            }
        }
    }

    auto new_g2 = polynomial[change_idx] * libff::alt_bn128_G2::one();
    if (change_idx == 0) {
        for_common_pk_g2s_[local_member_index_] = new_g2;
    } else {
        for_common_pk_g2s_[local_member_index_] = polynomial[0] * libff::alt_bn128_G2::one();
    }

    auto dkg_instance = std::make_shared<libBLS::Dkg>(valid_t, valid_n);
    local_src_secret_key_contribution_ = dkg_instance->SecretKeyContribution(
        polynomial, valid_n, valid_t);
    auto val = libBLS::ThresholdUtils::fieldElementToString(
        local_src_secret_key_contribution_[local_member_index_]);
    prefix_db_->SaveSwapKey(
        local_member_index_, elect_hegiht_, local_member_index_, local_member_index_, val);

#ifdef ZJC_UNITEST
    g2_vec_.clear();
    g2_vec_.push_back(polynomial[0] * libff::alt_bn128_G2::one());
#endif // ZJC_UNITTEST

    bls::protobuf::VerifyVecBrdReq bls_verify_req;
    bls_verify_req.set_change_idx(change_idx);
    bls::protobuf::VerifyVecItem& verify_item = *bls_verify_req.add_verify_vec();
    verify_item.set_x_c0(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(new_g2.X.c0)));
    verify_item.set_x_c1(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(new_g2.X.c1)));
    verify_item.set_y_c0(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(new_g2.Y.c0)));
    verify_item.set_y_c1(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(new_g2.Y.c1)));
    verify_item.set_z_c0(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(new_g2.Z.c0)));
    verify_item.set_z_c1(common::Encode::HexDecode(
        libBLS::ThresholdUtils::fieldElementToString(new_g2.Z.c1)));
    
    auto str = bls_verify_req.SerializeAsString();
    prefix_db_->AddBlsVerifyG2(security_->GetAddress(), bls_verify_req);
    valid_swapkey_set_.insert(local_member_index_);
    ++valid_sec_key_count_;
}

void BlsDkg::CreateDkgMessage(transport::MessagePtr& msg_ptr) {
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    if (bls_msg.has_finish_req()) {
        dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
        msg.set_des_dht_key(dht_key.StrKey());
    } else {
        dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
        msg.set_des_dht_key(dht_key.StrKey());
    }

    msg.set_type(common::kBlsMessage);
    auto broad_param = msg.mutable_broadcast();
    bls_msg.set_elect_height(elect_hegiht_);
    bls_msg.set_index(local_member_index_);
    transport::TcpTransport::Instance()->SetMessageHash(msg, msg_ptr->thread_idx);
    std::string message_hash;
    protos::GetProtoHash(msg_ptr->header, &message_hash);
    std::string sign_out;
    int sign_res = security_->Sign(message_hash, &sign_out);
    if (sign_res != security::kSecuritySuccess) {
        BLS_ERROR("signature error.");
        return;
    }

    msg.set_sign(sign_out);
}

};  // namespace bls

};  // namespace zjchain
 