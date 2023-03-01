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

namespace tmblock {

static const std::string kTimeBlockGidPrefix = common::Encode::HexDecode(
    "c575ff0d3eea61205e3433495431e312056d0d51a64c6badfd4ad8cc092b7daa");
TimeBlockManager* TimeBlockManager::Instance() {
    static TimeBlockManager ins;
    return &ins;
}

uint64_t TimeBlockManager::LatestTimestamp() {
    return latest_time_block_tm_;
}

uint64_t TimeBlockManager::LatestTimestampHeight() {
    return latest_time_block_height_;
}

TimeBlockManager::TimeBlockManager() {
    check_bft_tick_.CutOff(
        35 * kCheckTimeBlockPeriodUs,
        std::bind(&TimeBlockManager::CheckBft, this));
}

TimeBlockManager::~TimeBlockManager() {}

int TimeBlockManager::BackupCheckTimeBlockTx(const pools::protobuf::TxMessage& tx_info) {
//     if (tx_info.attr_size() != 2) {
//         TMBLOCK_ERROR("tx_info.attr_size() error: %d", tx_info.attr_size());
//         return kTimeBlockError;
//     }
// 
//     if (tx_info.attr(1).key() != kVssRandomAttr) {
//         TMBLOCK_ERROR("tx_info.attr(1).key() error: %s", tx_info.attr(1).key().c_str());
//         return kTimeBlockError;
//     }
// 
//     uint64_t leader_final_cons_random = 0;
//     if (!common::StringUtil::ToUint64(tx_info.attr(1).value(), &leader_final_cons_random)) {
//         return kTimeBlockError;
//     }

//     if (leader_final_cons_random != vss::VssManager::Instance()->GetConsensusFinalRandom()) {
//         TMBLOCK_ERROR("leader_final_cons_random: %lu, GetConsensusFinalRandom(): %lu",
//             leader_final_cons_random,
//             vss::VssManager::Instance()->GetConsensusFinalRandom());
//         return kTimeBlockVssError;
//     }
// 
    if (tx_info.key() != kAttrTimerBlock) {
        TMBLOCK_ERROR("tx_info.attr(0).key() error: %s", tx_info.key().c_str());
        return kTimeBlockError;
    }

    uint64_t leader_tm = 0;
    if (!common::StringUtil::ToUint64(tx_info.value(), &leader_tm)) {
        return kTimeBlockError;
    }

    if (!BackupheckNewTimeBlockValid(leader_tm)) {
        TMBLOCK_ERROR("BackupheckNewTimeBlockValid error: %llu", leader_tm);
        return kTimeBlockError;
    }

    return kTimeBlockSuccess;
}

bool TimeBlockManager::LeaderCanCallTimeBlockTx(uint64_t tm_sec) {
    uint64_t now_sec = common::TimeUtils::TimestampSeconds();
    if (now_sec >= latest_time_block_tm_ + common::kTimeBlockCreatePeriodSeconds) {
        return true;
    }

    if (now_sec  >= latest_tm_block_local_sec_ + common::kTimeBlockCreatePeriodSeconds) {
        return true;
    }

    return false;
}

void TimeBlockManager::CreateTimeBlockTx() {
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
    pools_mgr_->HandleMessage(msg_ptr);
    TMBLOCK_INFO("dispatch timeblock tx info success: %lu, vss: %s, real: %s!",
        new_time_block_tm, 0, tx_info.value().c_str());
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

bool TimeBlockManager::BackupheckNewTimeBlockValid(uint64_t new_time_block_tm) {
    uint64_t backup_latest_time_block_tm = latest_time_block_tm_;
    backup_latest_time_block_tm += common::kTimeBlockCreatePeriodSeconds;
    if (new_time_block_tm < (backup_latest_time_block_tm + kTimeBlockTolerateSeconds) &&
            new_time_block_tm >(backup_latest_time_block_tm - kTimeBlockTolerateSeconds)) {
        return true;
    }

    ZJC_ERROR("BackupheckNewTimeBlockValid error[%llu][%llu] latest_time_block_tm_[%lu]",
        new_time_block_tm, (uint64_t)backup_latest_time_block_tm, (uint64_t)latest_time_block_tm_);
    return false;
}

void TimeBlockManager::CheckBft() {
//     int32_t pool_mod_num = elect::ElectManager::Instance()->local_node_pool_mod_num();
//     if (pool_mod_num >= 0) {
//         consensus::BftManager::Instance()->StartBft("", pool_mod_num);
//     }
// 
//     check_bft_tick_.CutOff(
//         kCheckBftPeriodUs,
//         std::bind(&TimeBlockManager::CheckBft, this));
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

}  // namespace tmblock

}  // namespace zjchain
 