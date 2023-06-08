#include "vss/vss_manager.h"

#include "common/time_utils.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos/get_proto_hash.h"
#include "transport/processor.h"

namespace zjchain {

namespace vss {

VssManager::VssManager(std::shared_ptr<security::Security>& security_ptr)
        : security_ptr_(security_ptr) {
    network::Route::Instance()->RegisterMessage(
        common::kVssMessage,
        std::bind(&VssManager::HandleMessage, this, std::placeholders::_1));
//     transport::Processor::Instance()->RegisterProcessor(
//         common::kPoolTimerMessage,
//         std::bind(&VssManager::ConsensusTimerMessage, this, std::placeholders::_1));
}

uint64_t VssManager::EpochRandom() {
    return epoch_random_;
}

void VssManager::OnTimeBlock(
        uint64_t tm_block_tm,
        uint64_t tm_height,
        uint64_t epoch_random) {
    auto& elect_item = elect_item_[elect_valid_index_];
    if (elect_item.members == nullptr) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId &&
            common::GlobalInfo::Instance()->network_id() !=
            (network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset)) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        if (prev_tm_height_ != common::kInvalidUint64 && prev_tm_height_ >= tm_height) {
            ZJC_ERROR("prev_tm_height_ >= tm_height[%lu][%lu].", prev_tm_height_, tm_height);
            return;
        }

        if (elect_item_[elect_valid_index_].local_index == elect::kInvalidMemberIndex) {
            ZJC_ERROR("not elected.");
            return;
        }
    }

    ClearAll();
    local_random_.OnTimeBlock(tm_block_tm);
    ZJC_DEBUG("OnTimeBlock comming tm_block_tm: %lu, tm_height: %lu, elect_height: %lu, epoch_random: %lu, local hash: %lu",
    tm_block_tm, tm_height, elect_item.elect_height, epoch_random, local_random_.GetHash());
    epoch_random_ = epoch_random;
    latest_tm_block_tm_ = tm_block_tm;
    prev_tm_height_ = tm_height;
    int64_t local_offset_us = 0;
    auto tmblock_tm = tm_block_tm * 1000l * 1000l;
    auto offset_tm = 30l * 1000l * 1000l;
    kDkgPeriodUs = common::kTimeBlockCreatePeriodSeconds / 10 * 1000u * 1000u;
    if (tmblock_tm + kDkgPeriodUs + offset_tm < common::TimeUtils::TimestampUs()) {
        // ignore
        ZJC_ERROR("may be local time invalid, tmblock tm: %lu, "
            "tmblock_tm + kDkgPeriodUs + offset_tm: %lu, local tm: %lu",
            tmblock_tm, kDkgPeriodUs, offset_tm,
            (tmblock_tm + kDkgPeriodUs + offset_tm),
            common::TimeUtils::TimestampUs());
        return;
    }

    begin_time_us_ = common::TimeUtils::TimestampUs();
    first_offset_ = tmblock_tm + kDkgPeriodUs;
    second_offset_ = tmblock_tm + kDkgPeriodUs * 4;
    third_offset_ = tmblock_tm + kDkgPeriodUs * 8;
    if (begin_time_us_ < (int64_t)tmblock_tm + offset_tm) {
        kDkgPeriodUs = (common::kTimeBlockCreatePeriodSeconds - 20) * 1000l * 1000l / 10l;
        first_offset_ = tmblock_tm + offset_tm;
        begin_time_us_ = tmblock_tm + offset_tm - kDkgPeriodUs;
        second_offset_ = first_offset_ + kDkgPeriodUs * 3;
        third_offset_ = first_offset_ + kDkgPeriodUs * 7;
    }

    end_tm_ = tmblock_tm + common::kTimeBlockCreatePeriodSeconds * 1000000lu;
    ZJC_DEBUG("tmblock_tm: %lu, begin_time_us_: %lu, first_offset_: %lu, second_offset_: %lu, third_offset_: %lu, kDkgPeriodUs: %lu, local hash: %lu",
        tmblock_tm, begin_time_us_, first_offset_, second_offset_, third_offset_, kDkgPeriodUs, local_random_.GetHash());
}

void VssManager::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    if (network::DhtManager::Instance()->valid_count(
            common::GlobalInfo::Instance()->network_id()) <
            common::GlobalInfo::Instance()->sharding_min_nodes_count()) {
        return;
    }

    auto now_tm_us = common::TimeUtils::TimestampUs();
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        BroadcastFirstPeriodHash(msg_ptr->thread_idx);
        BroadcastSecondPeriodRandom(msg_ptr->thread_idx);
        BroadcastThirdPeriodRandom(msg_ptr->thread_idx);
    }

    auto etime = common::TimeUtils::TimestampUs();
    if (etime - now_tm_us >= 10000lu) {
        ZJC_DEBUG("VssManager handle message use time: %lu", (etime - now_tm_us));
    }
}

void VssManager::OnNewElectBlock(
        uint32_t sharding_id, 
        uint64_t elect_height,
        common::MembersPtr& members) {
    if (sharding_id == network::kRootCongressNetworkId &&
            common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        auto index = (elect_valid_index_ + 1) % 2;
        elect_item_[index].members = members;
        elect_item_[index].local_index = elect::kInvalidMemberIndex;
        for (uint32_t i = 0; i < members->size(); ++i) {
            if ((*members)[i]->id == security_ptr_->GetAddress()) {
                elect_item_[index].local_index = i;
                break;
            }
        }        
        
        elect_item_[index].member_count = members->size();
        elect_item_[index].elect_height = elect_height;
        elect_valid_index_ = index;
    }
}

uint64_t VssManager::GetConsensusFinalRandom() {
    if (max_count_random_ != 0) {
        return max_count_random_;
    }

    if (prev_max_count_random_ != 0) {
        return prev_max_count_random_;
    }

    return epoch_random_;
}

void VssManager::ClearAll() {
    begin_time_us_ = 0;
    local_random_.ResetStatus();
    for (uint32_t i = 0; i < common::kEachShardMaxNodeCount; ++i) {
        other_randoms_[i].ResetStatus();
    }

    first_period_cheched_ = false;
    second_period_cheched_ = false;
    third_period_cheched_ = false;
    final_consensus_nodes_.clear();
    final_consensus_random_count_.clear();
    prev_max_count_random_ = max_count_random_;
    max_count_random_ = 0;
    first_offset_ = 0;
    second_offset_ = 0;
    third_offset_ = 0;
    first_try_times_ = 0;
    second_try_times_ = 0;
    third_try_times_ = 0;
    end_tm_ = 0;
    max_count_ = 0;
}

uint64_t VssManager::GetAllVssValid() {
    uint64_t final_random = 0;
    auto& elect_item = elect_item_[elect_valid_index_];
    for (uint32_t i = 0; i < elect_item.member_count; ++i) {
        if (other_randoms_[i].IsRandomValid()) {
            final_random ^= other_randoms_[i].GetFinalRandomNum();
        }
    }

    return final_random;
}

bool VssManager::IsVssFirstPeriodsHandleMessage() {
#ifdef ZJC_UNITTEST
    return true;
#endif
    if (begin_time_us_ == 0) {
        return false;
    }

    auto now_tm_us = common::TimeUtils::TimestampUs();
    if ((int64_t)now_tm_us < (begin_time_us_ + kDkgPeriodUs * 4)) {
        ZJC_DEBUG("IsVssFirstPeriodsHandleMessage now_tm_us: %lu, begin_time_us_: %lu, (begin_time_us_ + kDkgPeriodUs * 4): %lu",
            now_tm_us, begin_time_us_, (begin_time_us_ + kDkgPeriodUs * 4));
        return true;
    }

//     ZJC_DEBUG("IsVssFirstPeriodsHandleMessage now_tm_us: %lu, (begin_time_us_ + kDkgPeriodUs * 4): %lu",
//         now_tm_us, (begin_time_us_ + kDkgPeriodUs * 4));
    return false;
}

bool VssManager::IsVssSecondPeriodsHandleMessage() {
#ifdef ZJC_UNITTEST
    return true;
#endif
    if (begin_time_us_ == 0) {
        return false;
    }

    auto now_tm_us = common::TimeUtils::TimestampUs();
    if ((int64_t)now_tm_us < (begin_time_us_ + kDkgPeriodUs * 8) &&
            (int64_t)now_tm_us >= (begin_time_us_ + kDkgPeriodUs * 4)) {
        return true;
    }

//     ZJC_DEBUG("IsVssSecondPeriodsHandleMessage now_tm_us: %lu, (begin_time_us_ + kDkgPeriodUs * 8): %lu, (begin_time_us_ + kDkgPeriodUs * 4): %lu",
//         now_tm_us, (begin_time_us_ + kDkgPeriodUs * 8), (begin_time_us_ + kDkgPeriodUs * 4));
    return false;
}

bool VssManager::IsVssThirdPeriodsHandleMessage() {
#ifdef ZJC_UNITTEST
    return true;
#endif
    if (begin_time_us_ == 0) {
        return false;
    }

    auto now_tm_us = common::TimeUtils::TimestampUs();
    if ((int64_t)now_tm_us >= (begin_time_us_ + kDkgPeriodUs * 8)) {
        return true;
    }
// 
//     ZJC_DEBUG("IsVssThirdPeriodsHandleMessage now_tm_us: %lu, (begin_time_us_ + kDkgPeriodUs * 8): %lu",
//         now_tm_us, (begin_time_us_ + kDkgPeriodUs * 8));
    return false;
}

void VssManager::BroadcastFirstPeriodHash(uint8_t thread_idx) {
    if (elect_item_[elect_valid_index_].members == nullptr) {
        return;
    }

    auto now_us = common::TimeUtils::TimestampUs();
    auto rand_tm = std::rand() % 60000000lu;
    if (first_prev_tm_ + 1000000lu >= now_us) {
        return;
    }
    if (first_try_times_ >= 3) {
        return;
    }

    if (!IsVssFirstPeriodsHandleMessage()) {
        return;
    }

    ++first_try_times_;
    first_prev_tm_ = now_us;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    transport::protobuf::Header& msg = msg_ptr->header;
    msg.set_type(common::kVssMessage);
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
    msg.set_src_sharding_id(network::kRootCongressNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    auto broadcast = msg.mutable_broadcast();
    vss::protobuf::VssMessage& vss_msg = *msg.mutable_vss_proto();
    vss_msg.set_random_hash(local_random_.GetHash());
    vss_msg.set_tm_height(prev_tm_height_);
    vss_msg.set_elect_height(prev_elect_height_);
    auto& elect_item = elect_item_[elect_valid_index_];
    vss_msg.set_member_index(elect_item.local_index);
    vss_msg.set_type(kVssRandomHash);
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    std::string message_hash;
    protos::GetProtoHash(msg, &message_hash);
    std::string sign;
    if (security_ptr_->Sign(
            message_hash,
            &sign) != security::kSecuritySuccess) {
        ZJC_ERROR("signature error.");
        return;
    }

    msg.set_sign(sign);
    network::Route::Instance()->Send(msg_ptr);
    HandleFirstPeriodHash(msg.vss_proto());
    ZJC_DEBUG("BroadcastFirstPeriodHash: %lu，prev_elect_height_: %lu", local_random_.GetHash(), prev_elect_height_);
#ifdef ZJC_UNITTEST
    first_msg_ = msg;
#endif
}

void VssManager::BroadcastSecondPeriodRandom(uint8_t thread_idx) {
    if (first_try_times_ <= 0) {
        return;
    }

    auto now_us = common::TimeUtils::TimestampUs();
    auto rand_tm = std::rand() % 60000000lu;
    if (second_prev_tm_ + 1000000lu >= now_us) {
        return;
    }
    if (second_try_times_ >= 3) {
        return;
    }

    if (!IsVssSecondPeriodsHandleMessage()) {
        return;
    }

    ++second_try_times_;
    second_prev_tm_ = now_us;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    transport::protobuf::Header& msg = msg_ptr->header;
    msg.set_type(common::kVssMessage);
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
    msg.set_src_sharding_id(network::kRootCongressNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    auto broadcast = msg.mutable_broadcast();
    vss::protobuf::VssMessage& vss_msg = *msg.mutable_vss_proto();
    vss_msg.set_random(local_random_.GetFinalRandomNum());
    vss_msg.set_tm_height(prev_tm_height_);
    vss_msg.set_elect_height(prev_elect_height_);
    auto& elect_item = elect_item_[elect_valid_index_];
    vss_msg.set_member_index(elect_item.local_index);
    vss_msg.set_type(kVssRandom);
    std::string message_hash;
    protos::GetProtoHash(msg, &message_hash);
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    std::string sign;
    if (security_ptr_->Sign(
            message_hash,
            &sign) != security::kSecuritySuccess) {
        ZJC_ERROR("signature error.");
        return;
    }

    msg.set_sign(sign);
    network::Route::Instance()->Send(msg_ptr);
    HandleSecondPeriodRandom(msg.vss_proto());
    ZJC_DEBUG("BroadcastSecondPeriodRandom: %lu，prev_elect_height_: %lu", local_random_.GetFinalRandomNum(), prev_elect_height_);
#ifdef ZJC_UNITTEST
    second_msg_ = msg;
#endif
}

void VssManager::BroadcastThirdPeriodRandom(uint8_t thread_idx) {
    if (first_try_times_ <= 0) {
        return;
    }

    auto now_us = common::TimeUtils::TimestampUs();
    auto rand_tm = std::rand() % 60000000lu;
    if (third_prev_tm_ + 1000000lu >= now_us) {
        return;
    }
    if (third_try_times_ >= 3) {
        return;
    }

    if (!IsVssThirdPeriodsHandleMessage()) {
        return;
    }

    ++third_try_times_;
    third_prev_tm_ = now_us;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    transport::protobuf::Header& msg = msg_ptr->header;
    msg.set_type(common::kVssMessage);
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
    msg.set_src_sharding_id(network::kRootCongressNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    auto broadcast = msg.mutable_broadcast();
    vss::protobuf::VssMessage& vss_msg = *msg.mutable_vss_proto();
    vss_msg.set_random(GetAllVssValid());
    vss_msg.set_tm_height(prev_tm_height_);
    vss_msg.set_elect_height(prev_elect_height_);
    vss_msg.set_type(kVssFinalRandom);
    auto& elect_item = elect_item_[elect_valid_index_];
    vss_msg.set_member_index(elect_item.local_index);
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    std::string message_hash;
    protos::GetProtoHash(msg, &message_hash);
    std::string sign;
    if (security_ptr_->Sign(
            message_hash,
            &sign) != security::kSecuritySuccess) {
        ZJC_ERROR("signature error.");
        return;
    }

    msg.set_sign(sign);
    network::Route::Instance()->Send(msg_ptr);
    HandleThirdPeriodRandom(msg.vss_proto());
    ZJC_DEBUG("BroadcastThirdPeriodRandom: %lu，prev_elect_height_: %lu", GetAllVssValid(), prev_elect_height_);
#ifdef ZJC_UNITTEST
    third_msg_ = msg;
#endif
}

void VssManager::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    assert(header.type() == common::kVssMessage);
    ZJC_DEBUG("vss message coming.");
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId &&
            common::GlobalInfo::Instance()->network_id() !=
            (network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset)) {
        ZJC_DEBUG("invalid vss message network_id: %d", common::GlobalInfo::Instance()->network_id());
        return;
    }

    if (elect_item_[elect_valid_index_].local_index == elect::kInvalidMemberIndex) {
        ZJC_ERROR("not elected.");
        return;
    }

    // must verify message signature, to avoid evil node
    auto& vss_msg = header.vss_proto();
    auto& elect_item = elect_item_[elect_valid_index_];
    if (vss_msg.member_index() >= elect_item.member_count) {
        ZJC_ERROR("member index invalid.");
        return;
    }

    auto& pubkey = (*elect_item.members)[vss_msg.member_index()]->pubkey;
    std::string message_hash;
    protos::GetProtoHash(header, &message_hash);
    if (security_ptr_->Verify(message_hash, pubkey, header.sign()) != security::kSecuritySuccess) {
        ZJC_ERROR("security::Security::Instance()->Verify failed");
        return;
    }

    switch (vss_msg.type()) {
    case kVssRandomHash:
        HandleFirstPeriodHash(vss_msg);
        break;
    case kVssRandom:
        HandleSecondPeriodRandom(vss_msg);
        break;
    case kVssFinalRandom:
        HandleThirdPeriodRandom(vss_msg);
        break;
    default:
        break;
    }
}

void VssManager::HandleFirstPeriodHash(const protobuf::VssMessage& vss_msg) {
    if (!IsVssFirstPeriodsHandleMessage()) {
        ZJC_DEBUG("invalid first period message.");
        return;
    }

    auto& elect_item = elect_item_[elect_valid_index_];
    auto& id = (*elect_item.members)[vss_msg.member_index()]->id;
    other_randoms_[vss_msg.member_index()].SetHash(id, vss_msg.random_hash());
    ZJC_DEBUG("HandleFirstPeriodHash: %s, %llu",
        common::Encode::HexEncode(id).c_str(), vss_msg.random_hash());
}

void VssManager::HandleSecondPeriodRandom(const protobuf::VssMessage& vss_msg) {
    if (!IsVssSecondPeriodsHandleMessage()) {
        ZJC_DEBUG("invalid second period message.");
        return;
    }


    auto& elect_item = elect_item_[elect_valid_index_];
    if (vss_msg.member_index() >= elect_item.members->size()) {
        ZJC_WARN("invalid member index: %u, %u", vss_msg.member_index(), elect_item.members->size());
        return;
    }

    auto& id = (*elect_item.members)[vss_msg.member_index()]->id;
    other_randoms_[vss_msg.member_index()].SetFinalRandomNum(id, vss_msg.random());
    ZJC_DEBUG("HandleSecondPeriodRandom: %s, %llu",
        common::Encode::HexEncode(id).c_str(), vss_msg.random());
}

void VssManager::SetConsensusFinalRandomNum(const std::string& id, uint64_t final_random_num) {
    // random hash must coming
    auto iter = final_consensus_nodes_.find(id);
    if (iter != final_consensus_nodes_.end()) {
        ZJC_ERROR("invalid id: %s, final_random_num: %lu", common::Encode::HexEncode(id).c_str(), final_random_num);
        return;
    }

    final_consensus_nodes_.insert(id);
    auto count_iter = final_consensus_random_count_.find(final_random_num);
    if (count_iter == final_consensus_random_count_.end()) {
        final_consensus_random_count_[final_random_num] = 1;
        return;
    }

    ++count_iter->second;
    if (max_count_ < count_iter->second) {
        max_count_ = count_iter->second;
        max_count_random_ = final_random_num;
    }

}

void VssManager::HandleThirdPeriodRandom(const protobuf::VssMessage& vss_msg) {
    auto& elect_item = elect_item_[elect_valid_index_];
    if (vss_msg.member_index() >= elect_item.members->size()) {
        return;
    }

    auto& id = (*elect_item.members)[vss_msg.member_index()]->id;
    if (!IsVssThirdPeriodsHandleMessage()) {
        ZJC_ERROR("not IsVssThirdPeriodsHandleMessage, id: %s",
            common::Encode::HexEncode(id).c_str());
        return;
    }

    SetConsensusFinalRandomNum(id, vss_msg.random());
    ZJC_DEBUG("HandleThirdPeriodRandom: %s, %llu, max_count_random_: %lu",
        common::Encode::HexEncode(id).c_str(), vss_msg.random(), max_count_random_);
}

}  // namespace vss

}  // namespace zjchain
