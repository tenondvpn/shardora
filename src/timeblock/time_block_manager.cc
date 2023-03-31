#include "timeblock/time_block_manager.h"

#include <cstdlib>

#include "common/global_info.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "common/user_property_key_define.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "protos//get_proto_hash.h"
#include "protos/prefix_db.h"
#include "protos/pools.pb.h"
#include "timeblock/time_block_utils.h"
#include "transport/tcp_transport.h"
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
    ZJC_DEBUG("success create timeblock tx.");
}

void TimeBlockManager::NewBlockWithTx(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    ZJC_DEBUG("timeblock new tx coming: %lu, storage size: %d", block_item->height(), tx.storages_size());
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == kAttrTimerBlock) {
            common::Split<> items(tx.storages(i).val_hash().c_str(), '_');
            if (items.Count() != 2) {
                assert(false);
                return;
            }

            uint64_t tm = 0;
            uint64_t vss = 0;
            if (!common::StringUtil::ToUint64(items[0], &tm)) {
                assert(false);
                return;
            }

            if (!common::StringUtil::ToUint64(items[1], &vss)) {
                assert(false);
                return;
            }

            UpdateTimeBlock(block_item->height(), tm, vss);
            break;
        }
    }
}

void TimeBlockManager::BroadcastTimeblock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item) {
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(network::kRootCongressNetworkId);
    dht::DhtKeyManager dht_key(network::kNodeNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    auto& bft_msg = msg.mutable_zbft();
    *bft_msg.mutable_block() = *block_item;
    bft_msg.set_pool_index(common::kRootChainPoolIndex);
    auto* brdcast = msg.mutable_broadcast();
    std::string msg_hash;
    protos::GetProtoHash(msg, &msg_hash);
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    network::Route::Instance()->Send(msg_ptr);
}

void TimeBlockManager::UpdateTimeBlock(
        uint64_t latest_time_block_height,
        uint64_t latest_time_block_tm,
        uint64_t vss_random) {
    if (latest_time_block_height_ != common::kInvalidUint64 &&
            latest_time_block_height_ >= latest_time_block_height) {
        assert(false);
        return;
    }

    ZJC_DEBUG("LeaderNewTimeBlockValid height[%lu:%lu], tm[%lu:%lu], vss[%lu]",
        latest_time_block_height,
        latest_time_block_height_,
        latest_time_block_tm,
        latest_time_block_tm_,
        vss_random);
    latest_time_block_height_ = latest_time_block_height;
    latest_time_block_tm_ = latest_time_block_tm;
    latest_tm_block_local_sec_ = common::TimeUtils::TimestampSeconds();
    CreateTimeBlockTx();
    prefix_db_->SaveLatestTimeBlock(
        latest_time_block_height,
        latest_time_block_tm,
        vss_random);
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
        UpdateTimeBlock(tm_block.height(), tm_block.timestamp(), tm_block.vss_random());
        ZJC_DEBUG("init time block success: %lu, %lu, %lu",
            tm_block.timestamp(), tm_block.height(), tm_block.vss_random());
    }
}

}  // namespace timeblock

}  // namespace zjchain
 