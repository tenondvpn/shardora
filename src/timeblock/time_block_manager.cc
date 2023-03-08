#include "timeblock/time_block_manager.h"

#include <cstdlib>

#include "common/user_property_key_define.h"
#include "common/string_utils.h"
#include "common/global_info.h"
#include "network/network_utils.h"
#include "protos/prefix_db.h"
#include "protos/pools.pb.h"
#include "timeblock/time_block_utils.h"
#include "transport/transport_utils.h"
// #include "vss/vss_manager.h"

namespace zjchain {

namespace timeblock {

static const std::string kTimeBlockGidPrefix = common::Encode::HexDecode(
    "c575ff0d3eea61205e3433495431e312056d0d51a64c6badfd4ad8cc092b7daa");

uint64_t TimeBlockManager::LatestTimestamp() {
    return latest_time_block_tm_;
}

uint64_t TimeBlockManager::LatestTimestampHeight() {
    return latest_time_block_height_;
}

TimeBlockManager::TimeBlockManager() {}

TimeBlockManager::~TimeBlockManager() {}

void TimeBlockManager::CreateTimeBlockTx() {
    if (create_tm_tx_cb_ == nullptr) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId &&
            common::GlobalInfo::Instance()->network_id() !=
            (network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset)) {
        return;
    }

    auto gid = common::Hash::keccak256(kTimeBlockGidPrefix +
        std::to_string(latest_time_block_tm_));
    uint64_t new_time_block_tm = latest_time_block_tm_ + common::kTimeBlockCreatePeriodSeconds;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    pools::protobuf::TxMessage& tx_info = *msg_ptr->header.mutable_tx_proto();
    tx_info.set_step(pools::protobuf::kConsensusRootTimeBlock);
    tx_info.set_pubkey("");
    tx_info.set_to(common::kRootChainTimeBlockTxAddress);
    tx_info.set_gid(gid);
    tx_info.set_gas_limit(0llu);
    tx_info.set_amount(0);
    tx_info.set_gas_price(common::kBuildinTransactionGasPrice);
    tx_info.set_key(kAttrTimerBlock);
    tx_info.set_value(std::to_string(new_time_block_tm) + "_" + std::to_string(0));
    tmblock_tx_ptr_ = create_tm_tx_cb_(msg_ptr);
}

void TimeBlockManager::UpdateTimeBlock(
        uint64_t latest_time_block_height,
        uint64_t latest_time_block_tm,
        uint64_t vss_random) {
    if (latest_time_block_height_ != common::kInvalidUint64 &&
            latest_time_block_height_ >= latest_time_block_height) {
        return;
    }

    latest_time_block_height_ = latest_time_block_height;
    latest_time_block_tm_ = latest_time_block_tm;
    latest_tm_block_local_sec_ = common::TimeUtils::TimestampSeconds();
    CreateTimeBlockTx();
    prefix_db_->SaveLatestTimeBlock(
        latest_time_block_height,
        latest_time_block_tm,
        vss_random);
    ZJC_ERROR("LeaderNewTimeBlockValid offset_tm final[%lu], prev[%lu]",
        (uint64_t)latest_time_block_height_, (uint64_t)latest_time_block_tm_);
//     vss::VssManager::Instance()->OnTimeBlock(
//         latest_time_block_tm,
//         latest_time_block_height,
//         elect::ElectManager::Instance()->latest_height(
//             common::GlobalInfo::Instance()->network_id()),
//         vss_random);
//     elect::ElectManager::Instance()->OnTimeBlock(latest_time_block_tm);
}

void TimeBlockManager::LoadLatestTimeBlock() {
    timeblock::protobuf::TimeBlock tm_block;
    ZJC_DEBUG("init time block now.");
    if (prefix_db_->GetLatestTimeBlock(&tm_block)) {
        timeblock_ = std::make_shared<timeblock::protobuf::TimeBlock>(tm_block);
        UpdateTimeBlock(tm_block.timestamp(), tm_block.height(), tm_block.vss_random());
        ZJC_DEBUG("init time block success.");
    }
}

}  // namespace timeblock

}  // namespace zjchain
 