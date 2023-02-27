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
    min_aggree_member_count_ = t;
    member_count_ = n;
    local_sec_key_ = local_sec_key;
    local_publick_key_ = local_publick_key;
    common_public_key_ = common_public_key;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
}

void BlsDkg::Destroy() {
    dkg_verify_brd_timer_.Destroy();
    dkg_swap_seckkey_timer_.Destroy();
    dkg_finish_timer_.Destroy();
}

void BlsDkg::OnNewElectionBlock(
        uint64_t elect_height,
        common::MembersPtr& members) try {
    if (elect_height <= elect_hegiht_) {
        ZJC_DEBUG("elect height error: %lu, %lu", elect_height, elect_hegiht_);
        return;
    }

    memset(valid_swaped_keys_, 0, sizeof(valid_swaped_keys_));
    memset(has_swaped_keys_, 0, sizeof(has_swaped_keys_));
    finished_ = false;
    // destroy old timer
    dkg_verify_brd_timer_.Destroy();
    dkg_swap_seckkey_timer_.Destroy();
    dkg_finish_timer_.Destroy();
    max_finish_count_ = 0;
    max_finish_hash_ = "";
    valid_sec_key_count_ = 0;
    members_ = members;
    valid_swapkey_set_.clear();
//     memset(invalid_node_map_, 0, sizeof(invalid_node_map_));
    min_aggree_member_count_ = common::GetSignerCount(members_->size());
    member_count_ = members_->size();
    dkg_instance_ = std::make_shared<libBLS::Dkg>(min_aggree_member_count_, members_->size());
    elect_hegiht_ = elect_height;
    for (uint32_t i = 0; i < members_->size(); ++i) {
        if ((*members_)[i]->id == security_->GetAddress()) {
            local_member_index_ = i;
            break;
        }
    }

    //     auto tmblock_tm = tmblock::TimeBlockManager::Instance()->LatestTimestamp() * 1000l * 1000l;
    auto tmblock_tm = 1 * 1000l * 1000l;
    begin_time_us_ = common::TimeUtils::TimestampUs();
    auto ver_offset = kDkgPeriodUs;
    auto swap_offset = kDkgPeriodUs * 4;
    auto finish_offset = kDkgPeriodUs * 8;
    auto timeblock_period = common::kTimeBlockCreatePeriodSeconds * 1000l * 1000l;
    auto offset_period = timeblock_period / 3l;
    if (begin_time_us_ < tmblock_tm + offset_period) {
        kDkgPeriodUs = (timeblock_period - offset_period) / 10l;
        ver_offset = tmblock_tm + offset_period - begin_time_us_;
        begin_time_us_ = tmblock_tm + offset_period - kDkgPeriodUs;
        swap_offset = ver_offset + kDkgPeriodUs * 3;
        finish_offset = ver_offset + kDkgPeriodUs * 7;
    }

    ver_offset += rand() % kDkgPeriodUs;
    swap_offset += rand() % kDkgPeriodUs;
    finish_offset += rand() % kDkgPeriodUs;
    swapkey_valid_ = true;
#ifndef ZJC_UNITTEST
    dkg_verify_brd_timer_.CutOff(
        ver_offset,
        std::bind(&BlsDkg::BroadcastVerfify, this, std::placeholders::_1));
    dkg_swap_seckkey_timer_.CutOff(
        swap_offset,
        std::bind(&BlsDkg::SwapSecKey, this, std::placeholders::_1));
    dkg_finish_timer_.CutOff(
        finish_offset,
        std::bind(&BlsDkg::FinishNoLock, this, std::placeholders::_1));
    check_verify_brd_timer_.CutOff(
        3000000l,
        std::bind(&BlsDkg::CheckVerifyAllValid, this, std::placeholders::_1));
    check_swap_seckkey_timer_.CutOff(
        3000000l,
        std::bind(&BlsDkg::CheckSwapKeyAllValid, this, std::placeholders::_1));
#endif
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::HandleMessage(const transport::MessagePtr& msg_ptr) try {
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
    if (members_->size() <= bls_msg.index()) {
        BLS_ERROR("members_->size() <= bls_msg.index(): %d, %d",
            members_->size(), bls_msg.index());
        return;
    }

    if (bls_msg.index() >= members_->size()) {
        BLS_ERROR("bls_msg.index() >= members_->size()");
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

bool BlsDkg::IsSignValid(const protobuf::BlsMessage& bls_msg, std::string* content_to_hash) {
    if (bls_msg.has_verify_brd()) {
        for (int32_t i = 0; i < bls_msg.verify_brd().verify_vec_size(); ++i) {
            *content_to_hash += bls_msg.verify_brd().verify_vec(i).x_c0() +
                bls_msg.verify_brd().verify_vec(i).x_c1() +
                bls_msg.verify_brd().verify_vec(i).y_c0() +
                bls_msg.verify_brd().verify_vec(i).y_c1() +
                bls_msg.verify_brd().verify_vec(i).z_c0() +
                bls_msg.verify_brd().verify_vec(i).z_c1();
        }
    } else if (bls_msg.has_swap_req()) {
        for (int32_t i = 0; i < bls_msg.swap_req().keys_size(); ++i) {
            *content_to_hash += bls_msg.swap_req().keys(i).sec_key();
        }
    } else {
        return false;
    }

    *content_to_hash = common::Hash::keccak256(*content_to_hash);
    auto& pubkey = (*members_)[bls_msg.index()]->pubkey;
    if (security_->Verify(
            *content_to_hash,
            pubkey,
            bls_msg.sign()) != security::kSecuritySuccess) {
        BLS_INFO("bls create IsSignValid error block elect_height: %lu", elect_hegiht_);
        return false;
    }

    return true;
}

void BlsDkg::HandleVerifyBroadcast(const transport::MessagePtr& msg_ptr) try {
    auto& header = msg_ptr->header;
    auto& bls_msg = header.bls_proto();
    if (!IsVerifyBrdPeriod()) {
        return;
    }

    std::string msg_hash;
    if (!IsSignValid(bls_msg, &msg_hash)) {
        BLS_ERROR("sign verify failed!");
        return;
    }

    if (members_->size() <= bls_msg.index()) {
        return;
    }

    if (1 != (uint32_t)bls_msg.verify_brd().verify_vec_size()) {
        BLS_ERROR("min_aggree_member_count_ != "
            "bls_msg.verify_brd().verify_vec_size()[%d: %d]",
            1,
            bls_msg.verify_brd().verify_vec_size());
        assert(false);
        return;
    }

    prefix_db_->AddBlsVerifyG2(
        local_member_index_, bls_msg.verify_brd().verify_vec(0), elect_hegiht_, bls_msg.index(), 0);
    ZJC_DEBUG("save verify g2 success local: %d, %lu, %u, %u",
        local_member_index_, elect_hegiht_, bls_msg.index(), 0);
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::CheckVerifyAllValid(uint8_t thread_idx) {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    if (now_tm_us > (begin_time_us_ + kDkgPeriodUs * 2 + 5) &&
            now_tm_us < (begin_time_us_ + kDkgPeriodUs * 4 - 5)) {
        for (uint32_t i = 0; i < members_->size(); ++i) {
            if (i == local_member_index_) {
                continue;
            }

            auto iter = verify_map_.find(i);
            if (iter == verify_map_.end()) {
                SendGetVerifyInfo(thread_idx, i);
            }
        }
    }

    if (now_tm_us < (begin_time_us_ + kDkgPeriodUs * 4 - 5)) {
        check_verify_brd_timer_.CutOff(
            3000000l,
            std::bind(&BlsDkg::CheckVerifyAllValid, this, std::placeholders::_1));
    }
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
        for (uint32_t i = 0; i < members_->size(); ++i) {
            auto iter = valid_swapkey_set_.find(i);
            if (iter == valid_swapkey_set_.end()) {
                SendGetSwapKey(thread_idx, i);
            }
        }
    }

    if (now_tm_us < (begin_time_us_ + kDkgPeriodUs * 8 - 5)) {
        check_swap_seckkey_timer_.CutOff(
            3000000l,
            std::bind(&BlsDkg::CheckSwapKeyAllValid, this, std::placeholders::_1));
    }
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
//         assert(false);
        return;
    }

    if (bls_msg.swap_req().keys_size() <= (int32_t)local_member_index_) {
        ZJC_WARN("swap key size error: %d, %d",
            bls_msg.swap_req().keys_size(), local_member_index_);
        return;
    }

    std::string msg_hash;
    if (!IsSignValid(bls_msg, &msg_hash)) {
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

    // swap
    auto tmp_swap_key = libff::alt_bn128_Fr(sec_key.c_str());
    prefix_db_->SaveSwapKey(
        local_member_index_, elect_hegiht_, local_member_index_, bls_msg.index(), sec_key);
    // verify it valid, if not broadcast against.
    auto first_g2 = GetVerifyG2FromDb(bls_msg.index(), 0);
    auto verify_g2 = dkg_instance_->GetFirstVerification(
        bls_msg.index(),
        tmp_swap_key,
        first_g2);
    if (verify_g2 == libff::alt_bn128_G2::zero()) {
        BLS_DEBUG("get verify g2 failed!");
        return;
    }

    std::string gw_to_hash = common::Hash::keccak256(
        libBLS::ThresholdUtils::fieldElementToString(verify_g2.X.c0) +
        libBLS::ThresholdUtils::fieldElementToString(verify_g2.X.c1) +
        libBLS::ThresholdUtils::fieldElementToString(verify_g2.Y.c0) +
        libBLS::ThresholdUtils::fieldElementToString(verify_g2.Y.c1) +
        libBLS::ThresholdUtils::fieldElementToString(verify_g2.Z.c0) +
        libBLS::ThresholdUtils::fieldElementToString(verify_g2.Z.c1));
    if (gw_to_hash != bls_msg.swap_req().keys(local_member_index_).verify_hash()) {
        BLS_WARN("invalid verify g2.");
        return;
    }

    valid_swapkey_set_.insert(bls_msg.index());
    ++valid_sec_key_count_;
    has_swaped_keys_[bls_msg.index()] = true;
//     swap_key_map_[bls_msg.index()] = header.bls_proto().SerializeAsString();
    ZJC_DEBUG("success handle swap sec key: %d", valid_sec_key_count_);
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

libff::alt_bn128_G2 BlsDkg::GetVerifyG2FromDb(uint32_t first_index, uint32_t second_index) {
    bls::protobuf::VerifyVecItem item;
    auto res = prefix_db_->GetBlsVerifyG2(
        local_member_index_, elect_hegiht_, first_index, second_index, &item);
    if (!res) {
        ZJC_DEBUG("get verify g2 failed local: %d, %lu, %u, %u",
            local_member_index_, elect_hegiht_, first_index, second_index);
        return libff::alt_bn128_G2::zero();
    }

    auto x_c0 = libff::alt_bn128_Fq(item.x_c0().c_str());
    auto x_c1 = libff::alt_bn128_Fq(item.x_c1().c_str());
    auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
    auto y_c0 = libff::alt_bn128_Fq(item.y_c0().c_str());
    auto y_c1 = libff::alt_bn128_Fq(item.y_c1().c_str());
    auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
    auto z_c0 = libff::alt_bn128_Fq(item.z_c0().c_str());
    auto z_c1 = libff::alt_bn128_Fq(item.z_c1().c_str());
    auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
    return libff::alt_bn128_G2(x_coord, y_coord, z_coord);
}

void BlsDkg::BroadcastVerfify(uint8_t thread_idx) try {
    if (members_ == nullptr || local_member_index_ >= members_->size()) {
        return;
    }

    CreateContribution();
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    auto verfiy_brd = bls_msg.mutable_verify_brd();
    std::string content_to_hash;
    content_to_hash.reserve(1024);
    auto verify_item = verfiy_brd->add_verify_vec();
    auto res = prefix_db_->GetBlsVerifyG2(
       local_member_index_, elect_hegiht_, local_member_index_, 0, verify_item);
    if (!res) {
        return;
    }

    content_to_hash += verify_item->x_c0();
    content_to_hash += verify_item->x_c1();
    content_to_hash += verify_item->y_c0();
    content_to_hash += verify_item->y_c1();
    content_to_hash += verify_item->z_c0();
    content_to_hash += verify_item->z_c1();
    auto dht = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht) {
        return;
    }

    auto message_hash = common::Hash::keccak256(content_to_hash);
    CreateDkgMessage(dht->local_node(), bls_msg, message_hash, msg);
    auto broad_param = msg.mutable_broadcast();
    broad_param->set_hop_to_layer(0);
#ifdef ZJC_UNITTEST
    ver_brd_msg_ = msg;
#else
    network::Route::Instance()->Send(msg_ptr);
#endif
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::SwapSecKey(uint8_t thread_idx) try {
    if (members_ == nullptr || local_member_index_ >= members_->size()) {
        ZJC_ERROR("members invalid!");
        return;
    }

    if (local_src_secret_key_contribution_.size() != members_->size()) {
        ZJC_ERROR("local_src_secret_key_contribution_ size invalid!");
        return;
    }

    std::string content_to_hash;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    auto swap_req = bls_msg.mutable_swap_req();
    for (uint32_t i = 0; i < members_->size(); ++i) {
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

        auto first_g2 = GetVerifyG2FromDb(local_member_index_, 0);
        auto verify_g2 = dkg_instance_->GetFirstVerification(
            local_member_index_,
            local_src_secret_key_contribution_[local_member_index_],
            first_g2);
        if (verify_g2 == libff::alt_bn128_G2::zero()) {
            return;
        }

        std::string gw_to_hash = common::Hash::keccak256(
            libBLS::ThresholdUtils::fieldElementToString(verify_g2.X.c0) +
            libBLS::ThresholdUtils::fieldElementToString(verify_g2.X.c1) +
            libBLS::ThresholdUtils::fieldElementToString(verify_g2.Y.c0) +
            libBLS::ThresholdUtils::fieldElementToString(verify_g2.Y.c1) +
            libBLS::ThresholdUtils::fieldElementToString(verify_g2.Z.c0) +
            libBLS::ThresholdUtils::fieldElementToString(verify_g2.Z.c1));
        swap_item->set_sec_key(seckey);
        swap_item->set_sec_key_len(seckey_len);
        swap_item->set_verify_hash(gw_to_hash);
        content_to_hash += seckey;
    }

    auto dht = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht) {
        ZJC_ERROR("get network failed: %d", common::GlobalInfo::Instance()->network_id());
        return;
    }

    auto message_hash = common::Hash::keccak256(content_to_hash);
    CreateDkgMessage(dht->local_node(), bls_msg, message_hash, msg);
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    auto broad_param = msg.mutable_broadcast();
    broad_param->set_hop_to_layer(0);
#ifdef ZJC_UNITTEST
    sec_swap_msgs_ = msg;
    ZJC_DEBUG("success add swap msg");
#else
    network::Route::Instance()->Send(msg_ptr);
#endif
} catch (std::exception& e) {
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::CreateSwapKey(uint32_t member_idx, std::string* seckey, int32_t* seckey_len) {
    if (members_ == nullptr || local_member_index_ >= members_->size()) {
        return;
    }

    if (local_src_secret_key_contribution_.size() != members_->size()) {
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

void BlsDkg::FinishNoLock(uint8_t thread_idx) try {
    swapkey_valid_ = false;
    if (members_ == nullptr ||
            local_member_index_ >= members_->size() ||
            valid_sec_key_count_ < min_aggree_member_count_) {
        BLS_ERROR("elect_height: %lu, valid count error.valid_sec_key_count_: %d,"
            "min_aggree_member_count_: %d, members_ == nullptr: %d, local_member_index_: %d,"
            "members_->size(): %d",
            elect_hegiht_, valid_sec_key_count_, min_aggree_member_count_,
            (members_ == nullptr), local_member_index_, members_->size());
        return;
    }

    uint32_t bitmap_size = members_->size() / 64 * 64;
    if (members_->size() % 64 > 0) {
        bitmap_size += 64;
    }

    common::Bitmap bitmap(bitmap_size);
    common_public_key_ = libff::alt_bn128_G2::zero();
    std::vector<libff::alt_bn128_Fr> valid_seck_keys;
    for (size_t i = 0; i < members_->size(); ++i) {
        auto iter = valid_swapkey_set_.find(i);
        if (iter == valid_swapkey_set_.end()) {
            valid_seck_keys.push_back(libff::alt_bn128_Fr::zero());
            common_public_key_ = common_public_key_ + libff::alt_bn128_G2::zero();
            BLS_DEBUG("elect_height: %d, invalid swapkey index: %d", elect_hegiht_, i);
            continue;
        }

        auto g2_val = GetVerifyG2FromDb(i, 0);
        if (g2_val == libff::alt_bn128_G2::zero()) {
            valid_seck_keys.push_back(libff::alt_bn128_Fr::zero());
            common_public_key_ = common_public_key_ + libff::alt_bn128_G2::zero();
            BLS_DEBUG("elect_height: %d, invalid all_verification_vector index: %d",
                elect_hegiht_, i);
            continue;
        }

        std::string seckey;
        if (!prefix_db_->GetSwapKey(
            local_member_index_, elect_hegiht_, local_member_index_, i, &seckey)) {
            valid_seck_keys.push_back(libff::alt_bn128_Fr::zero());
            common_public_key_ = common_public_key_ + libff::alt_bn128_G2::zero();
            BLS_DEBUG("elect_height: %d, invalid secret_key_contribution_ index: %d",
                elect_hegiht_, i);
            continue;
        }

        valid_seck_keys.push_back(libff::alt_bn128_Fr(seckey.c_str()));
        common_public_key_ = common_public_key_ + g2_val;
        bitmap.Set(i);
    }

    uint32_t valid_count = static_cast<uint32_t>(
        (float)members_->size() * kBlsMaxExchangeMembersRatio);
    if (bitmap.valid_count() < valid_count) {
        BLS_ERROR("elect_height: %d, bitmap.valid_count: %u < %u, "
            "members_->size(): %u, kBlsMaxExchangeMembersRatio: %f",
            elect_hegiht_, bitmap.valid_count(), valid_count,
            members_->size(), kBlsMaxExchangeMembersRatio);
        return;
    }

    libBLS::Dkg dkg(min_aggree_member_count_, members_->size());
    local_sec_key_ = dkg.SecretKeyShareCreate(valid_seck_keys);
    local_publick_key_ = dkg.GetPublicKeyFromSecretKey(local_sec_key_);
    DumpLocalPrivateKey();
    BroadcastFinish(thread_idx, bitmap);
    finished_ = true;
} catch (std::exception& e) {
    local_sec_key_ = libff::alt_bn128_Fr::zero();
    BLS_ERROR("catch error: %s", e.what());
}

void BlsDkg::BroadcastFinish(uint8_t thread_idx, const common::Bitmap& bitmap) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    auto& bls_msg = *msg.mutable_bls_proto();
    auto finish_msg = bls_msg.mutable_finish_req();
    auto& data = bitmap.data();
    std::string msg_for_hash;
    for (auto iter = data.begin(); iter != data.end(); ++iter) {
        finish_msg->add_bitmap(*iter);
        msg_for_hash += std::to_string(*iter);
    }

    msg_for_hash += std::string("_") +
        std::to_string(common::GlobalInfo::Instance()->network_id());
    BLS_DEBUG("BroadcastFinish: %s", msg_for_hash.c_str());
    auto message_hash = common::Hash::keccak256(msg_for_hash);
    auto dht = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht) {
        return;
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
    std::string sign_x;
    std::string sign_y;
    if (bls_mgr_->Sign(
            min_aggree_member_count_,
            member_count_,
            local_sec_key_,
            message_hash,
            &sign_x,
            &sign_y) != kBlsSuccess) {
        return;
    }

    finish_msg->set_bls_sign_x(sign_x);
    finish_msg->set_bls_sign_y(sign_y);
    CreateDkgMessage(dht->local_node(), bls_msg, message_hash, msg);
    local_publick_key_.to_affine_coordinates();
#ifndef ZJC_UNITTEST
    network::Route::Instance()->Send(msg_ptr);
#endif
}

void BlsDkg::CreateContribution() {
    std::vector<libff::alt_bn128_Fr> polynomial = dkg_instance_->GeneratePolynomial();
    local_src_secret_key_contribution_ = dkg_instance_->SecretKeyContribution(polynomial);
    auto val = libBLS::ThresholdUtils::fieldElementToString(
        local_src_secret_key_contribution_[local_member_index_]);
    prefix_db_->SaveSwapKey(
        local_member_index_, elect_hegiht_, local_member_index_, local_member_index_, val);
    std::vector<libff::alt_bn128_G2> g2_vec(min_aggree_member_count_);
    for (size_t i = 0; i < min_aggree_member_count_; ++i) {
        g2_vec[i] = polynomial[i] * libff::alt_bn128_G2::one();
    }

    bls::protobuf::VerifyVecItem verify_item;
    verify_item.set_x_c0(libBLS::ThresholdUtils::fieldElementToString(g2_vec[0].X.c0));
    verify_item.set_x_c1(libBLS::ThresholdUtils::fieldElementToString(g2_vec[0].X.c1));
    verify_item.set_y_c0(libBLS::ThresholdUtils::fieldElementToString(g2_vec[0].Y.c0));
    verify_item.set_y_c1(libBLS::ThresholdUtils::fieldElementToString(g2_vec[0].Y.c1));
    verify_item.set_z_c0(libBLS::ThresholdUtils::fieldElementToString(g2_vec[0].Z.c0));
    verify_item.set_z_c1(libBLS::ThresholdUtils::fieldElementToString(g2_vec[0].Z.c1));
    prefix_db_->AddBlsVerifyG2(local_member_index_, verify_item, elect_hegiht_, local_member_index_, 0);
    valid_swapkey_set_.insert(local_member_index_);
    ++valid_sec_key_count_;
}

void BlsDkg::DumpContribution() {
//     nlohmann::json data;
//     data["idx"] = std::to_string(local_member_index_);
//     for (size_t i = 0; i < members_->size(); ++i) {
//         data["secret_key_contribution"][std::to_string(i)] =
//             libBLS::ThresholdUtils::fieldElementToString(
//                 all_secret_key_contribution_[local_member_index_][i]);
//     }
// 
//     for (size_t i = 0; i < min_aggree_member_count_; ++i) {
//         data["verification_vector"][std::to_string(i)]["X"]["c0"] =
//             libBLS::ThresholdUtils::fieldElementToString(
//                 all_verification_vector_[local_member_index_][i].X.c0);
//         data["verification_vector"][std::to_string(i)]["X"]["c1"] =
//             libBLS::ThresholdUtils::fieldElementToString(
//                 all_verification_vector_[local_member_index_][i].X.c1);
//         data["verification_vector"][std::to_string(i)]["Y"]["c0"] =
//             libBLS::ThresholdUtils::fieldElementToString(
//                 all_verification_vector_[local_member_index_][i].Y.c0);
//         data["verification_vector"][std::to_string(i)]["Y"]["c1"] =
//             libBLS::ThresholdUtils::fieldElementToString(
//                 all_verification_vector_[local_member_index_][i].Y.c1);
//         data["verification_vector"][std::to_string(i)]["Z"]["c0"] =
//             libBLS::ThresholdUtils::fieldElementToString(
//                 all_verification_vector_[local_member_index_][i].Z.c0);
//         data["verification_vector"][std::to_string(i)]["Z"]["c1"] =
//             libBLS::ThresholdUtils::fieldElementToString(
//                 all_verification_vector_[local_member_index_][i].Z.c1);
//     }
// 
//     std::ofstream outfile("data_for_" + std::to_string(local_member_index_) + "-th_participant.json");
//     outfile << data.dump(4) << "\n\n";
}

void BlsDkg::CreateDkgMessage(
        const dht::NodePtr& local_node,
        protobuf::BlsMessage& bls_msg,
        const std::string& message_hash,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    if (bls_msg.has_finish_req()) {
        dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
        msg.set_des_dht_key(dht_key.StrKey());
    } else {
        dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
        msg.set_des_dht_key(dht_key.StrKey());
    }

    msg.set_type(common::kBlsMessage);
    if (!message_hash.empty()) {
        std::string sign_out;
        int sign_res = security_->Sign(message_hash, &sign_out);
        if (sign_res != security::kSecuritySuccess) {
            BLS_ERROR("signature error.");
            return;
        }

        bls_msg.set_sign(sign_out);
    }
    
    bls_msg.set_elect_height(elect_hegiht_);
    bls_msg.set_index(local_member_index_);
}

};  // namespace bls

};  // namespace zjchain
