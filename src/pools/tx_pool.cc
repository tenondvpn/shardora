#include "pools/tx_pool.h"
#include <cassert>
#include <common/log.h>

#include "common/encode.h"
#include "common/utils.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_pool_manager.h"
#include "pools/tx_utils.h"

namespace shardora {

namespace pools {
    
TxPool::TxPool() {}

TxPool::~TxPool() {}

void TxPool::Init(
        TxPoolManager* pools_mgr,
        std::shared_ptr<security::Security> security,
        uint32_t pool_idx,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) {
    pools_mgr_ = pools_mgr;
    security_ = security;
    kv_sync_ = kv_sync;
    pool_index_ = pool_idx;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    InitLatestInfo();
    InitHeightTree();
}

void TxPool::InitHeightTree() {
    CheckThreadIdValid();
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    auto height_tree_ptr = std::make_shared<HeightTreeLevel>(
        net_id,
        pool_index_,
        latest_height_,
        db_);
    height_tree_ptr->Set(0);
    for (; synced_height_ <= latest_height_; ++synced_height_) {
        if (!height_tree_ptr->Valid(synced_height_ + 1)) {
            break;
        }
    }

    height_tree_ptr_ = height_tree_ptr;
}

uint32_t TxPool::SyncMissingBlocks(uint64_t now_tm_ms) {
    if (!height_tree_ptr_) {
        ZJC_DEBUG("get invalid height_tree_ptr_ size: %u, latest_height_: %lu", 0, latest_height_);
        return 0;
    }

//     if (prev_synced_time_ms_ >= now_tm_ms) {
//         return 0;
//     }
// 
    if (latest_height_ == common::kInvalidUint64) {
        // sync latest height from neighbors
        ZJC_DEBUG("get invalid heights size: %u, latest_height_: %lu", 0, latest_height_);
        return 0;
    }

//     prev_synced_time_ms_ = now_tm_ms + kSyncBlockPeriodMs;
    std::vector<uint64_t> invalid_heights;
    height_tree_ptr_->GetMissingHeights(&invalid_heights, latest_height_);
    ZJC_DEBUG("%u get invalid heights size: %u, latest_height_: %lu", pool_index_, invalid_heights.size(), latest_height_);
    if (invalid_heights.size() > 0) {
        auto net_id = common::GlobalInfo::Instance()->network_id();
        if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
            net_id -= network::kConsensusWaitingShardOffset;
        }

        if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
            return 0;
        }

        for (uint32_t i = 0; i < invalid_heights.size(); ++i) {
            if (prefix_db_->BlockExists(net_id, pool_index_, invalid_heights[i])) {
                height_tree_ptr_->Set(invalid_heights[i]);
//                 ZJC_DEBUG("pool exists des shard: %u, pool: %u, sync missing blocks latest height: %lu,"
//                     "invaid heights size: %u, height: %lu",
//                     net_id, pool_index_, latest_height_,
//                     invalid_heights.size(), invalid_heights[i]);
                continue;
            }

//             ZJC_DEBUG("pool des shard: %u, pool: %u, sync missing blocks latest height: %lu,"
//                 "invaid heights size: %u, height: %lu",
//                 net_id, pool_index_, latest_height_,
//                 invalid_heights.size(), invalid_heights[i]);
            ZJC_DEBUG("now add sync height 1, %u_%u_%lu", 
                net_id,
                pool_index_,
                invalid_heights[i]);
            kv_sync_->AddSyncHeight(
                net_id,
                pool_index_,
                invalid_heights[i],
                sync::kSyncHigh);
        }
    }

    return invalid_heights.size();
}

int TxPool::AddTx(TxItemPtr& tx_ptr) {
    if (added_txs_.size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        ZJC_DEBUG("add failed extend %u, %u, all valid: %u", 
            added_txs_.size(), common::GlobalInfo::Instance()->each_tx_pool_max_txs(), all_tx_size());
        return kPoolsError;
    }

    if (tx_ptr->tx_key.empty()) {
        ZJC_DEBUG("add failed unique hash empty: %d", tx_ptr->tx_info->step());
        tx_ptr->tx_key = pools::GetTxMessageHash(*tx_ptr->tx_info);
    }

    added_txs_.push(tx_ptr);
    ZJC_DEBUG("success add tx nonce: %lu", tx_ptr->tx_info->nonce());
    return kPoolsSuccess;
}

void TxPool::TxOver(view_block::protobuf::ViewBlockItem& view_block) {
    for (uint32_t i = 0; i < view_block.block_info().tx_list_size(); ++i) {
        auto& addr = IsTxUseFromAddress(view_block.block_info().tx_list(i).step()) ? 
            view_block.block_info().tx_list(i).from() : 
            view_block.block_info().tx_list(i).to();
        if (addr.empty()) {
            ZJC_DEBUG("addr is empty: %s", ProtobufToJson(view_block.block_info().tx_list(i)).c_str());
            assert(false);
            continue;
        }

        auto tx_key = GetTxKey(
            addr, 
            view_block.block_info().tx_list(i).nonce());
        auto iter = local_tx_map_.find(tx_key);
        if (iter != local_tx_map_.end()) {
            local_tx_map_.erase(iter);
        }

        ZJC_DEBUG("tx over tx key: %s", common::Encode::HexEncode(tx_key).c_str());
    }
}

void TxPool::GetTxSyncToLeader(
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckAddrNonceValidFunction gid_vlid_func) {
    TxItemPtr tx_ptr;
    while (txs->size() < count && added_txs_.pop(&tx_ptr)) {
        if (gid_vlid_func != nullptr && 
                !gid_vlid_func(tx_ptr->address_info->addr(), tx_ptr->tx_info->nonce())) {
            ZJC_DEBUG("nonce invalid: %s", tx_ptr->tx_info->nonce());
            continue;
        }

        if (!IsUserTransaction(tx_ptr->tx_info->step())) {
            ZJC_DEBUG("nonce invalid: %lu, step is not user tx: %d", 
                tx_ptr->tx_info->nonce(), 
                tx_ptr->tx_info->step());
        } else {
            auto* tx = txs->Add();
            *tx = *tx_ptr->tx_info;
        }

        local_tx_map_[tx_ptr->tx_key] = tx_ptr;
        local_tx_queue_.push(tx_ptr);
        tx_ptr->tx_info->set_tx_debug_timeout_seconds(common::TimeUtils::TimestampSeconds());
        ZJC_DEBUG("success to leader tx tx_key: %s, local_tx_map_ size: %u", 
            common::Encode::HexEncode(tx_ptr->tx_key).c_str(),
            local_tx_map_.size());
    }

    auto now_tm_seconds = common::TimeUtils::TimestampSeconds();
    while (!local_tx_queue_.empty()) {
        auto front_tx_ptr = local_tx_queue_.front();
        auto tmout = kUserPopedTxTimeoutSec;
        if (!IsUserTransaction(front_tx_ptr->tx_info->step())) {
            tmout = kSystemPopedTxTimeoutSec;
        }

        if (front_tx_ptr->tx_info->tx_debug_timeout_seconds() + tmout > now_tm_seconds) {
            break;
        }

        local_tx_queue_.pop();
        auto iter = local_tx_map_.find(front_tx_ptr->tx_key);
        if (iter == local_tx_map_.end()) {
            continue;
        }

        if (gid_vlid_func != nullptr && !gid_vlid_func(
                front_tx_ptr->address_info->addr(), 
                front_tx_ptr->tx_info->nonce())) {
            ZJC_DEBUG("tx_key invalid: %s", 
                common::Encode::HexEncode(front_tx_ptr->tx_key).c_str());
            break;
        }

        front_tx_ptr->tx_info->set_tx_debug_timeout_seconds(common::TimeUtils::TimestampSeconds());
        if (!IsUserTransaction(front_tx_ptr->tx_info->step())) {
            ZJC_DEBUG("tx_key invalid: %s, step is not user tx: %d", 
                common::Encode::HexEncode(front_tx_ptr->tx_key).c_str(), 
                front_tx_ptr->tx_info->step());
        } else {
            auto* tx = txs->Add();
            *tx = *front_tx_ptr->tx_info;
        }

        local_tx_queue_.push(front_tx_ptr);
    }
}

void TxPool::GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::map<std::string, TxItemPtr>& res_map,
        uint32_t count,
        pools::CheckAddrNonceValidFunction gid_vlid_func) {
    TxItemPtr tx_ptr;
    while (res_map.size() < count && added_txs_.pop(&tx_ptr)) {
        if (gid_vlid_func != nullptr && !gid_vlid_func(
                tx_ptr->address_info->addr(), 
                tx_ptr->tx_info->nonce())) {
            ZJC_DEBUG("tx_key invalid: %s",
                common::Encode::HexEncode(tx_ptr->tx_key).c_str());
            continue;
        }

        res_map[tx_ptr->tx_key] = tx_ptr;
        tx_ptr->tx_info->set_tx_debug_timeout_seconds(common::TimeUtils::TimestampSeconds());
        local_tx_map_[tx_ptr->tx_key] = tx_ptr;
        local_tx_queue_.push(tx_ptr);
        ZJC_DEBUG("tx_key success: %s, local_tx_map_ size: %u", 
            common::Encode::HexEncode(tx_ptr->tx_key).c_str(),
            local_tx_map_.size());
    }

    auto now_tm_seconds = common::TimeUtils::TimestampSeconds();
    while (!local_tx_queue_.empty()) {
        auto front_tx_ptr = local_tx_queue_.front();
        auto tmout = kUserPopedTxTimeoutSec;
        if (!IsUserTransaction(front_tx_ptr->tx_info->step())) {
            tmout = kSystemPopedTxTimeoutSec;
        }

        if (front_tx_ptr->tx_info->tx_debug_timeout_seconds() + tmout > now_tm_seconds) {
            break;
        }

        local_tx_queue_.pop();
        auto iter = local_tx_map_.find(front_tx_ptr->tx_key);
        if (iter == local_tx_map_.end()) {
            continue;
        }

        if (gid_vlid_func != nullptr && !gid_vlid_func(
                front_tx_ptr->address_info->addr(),
                front_tx_ptr->tx_info->nonce())) {
            ZJC_DEBUG("tx_key invalid: %s", 
                common::Encode::HexEncode(front_tx_ptr->tx_key).c_str());
            break;
        }

        front_tx_ptr->tx_info->set_tx_debug_timeout_seconds(common::TimeUtils::TimestampSeconds());
        res_map[front_tx_ptr->tx_key] = front_tx_ptr;
        local_tx_queue_.push(front_tx_ptr);
        ZJC_DEBUG("tx get tx key: %s", common::Encode::HexEncode(front_tx_ptr->tx_key).c_str());
    }

    while (res_map.size() < count && consensus_added_txs_.pop(&tx_ptr)) {
        if (gid_vlid_func != nullptr && !gid_vlid_func(
                tx_ptr->address_info->addr(), 
                tx_ptr->tx_info->nonce())) {
            ZJC_DEBUG("tx_key invalid: %s", 
                common::Encode::HexEncode(tx_ptr->tx_key).c_str());
            continue;
        }

        ZJC_DEBUG("tx_key success: %s", 
            common::Encode::HexEncode(tx_ptr->tx_key).c_str());
        res_map[tx_ptr->tx_key] = tx_ptr;
        ZJC_DEBUG("tx get tx key: %s", common::Encode::HexEncode(tx_ptr->tx_key).c_str());
    }

    if (local_tx_queue_.size() > 0)
    ZJC_INFO("now tx size added_txs_: %u, consensus_added_txs_: %u, local_tx_queue_: %u, local_tx_map_: %u", 
        added_txs_.size(), consensus_added_txs_.size(), local_tx_queue_.size(), local_tx_map_.size());
}

void TxPool::InitLatestInfo() {
    pools::protobuf::PoolLatestInfo pool_info;
    uint32_t network_id = common::GlobalInfo::Instance()->network_id();
    if (network_id == common::kInvalidUint32) {
        return;
    }

    if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
            network_id < network::kConsensusWaitingShardEndNetworkId) {
        network_id -= network::kConsensusWaitingShardOffset;
    }

    if (prefix_db_->GetLatestPoolInfo(
            network_id,
            pool_index_,
            &pool_info)) {
        // 根据数据库更新内存中的 tx_pool 状态
        if (latest_height_ == common::kInvalidUint64 || latest_height_ < pool_info.height()) {
            latest_height_ = pool_info.height();
            latest_hash_ = pool_info.hash();
            synced_height_ = pool_info.synced_height();
            latest_timestamp_ = pool_info.timestamp();
            prev_synced_height_ = synced_height_;
            to_sync_max_height_ = latest_height_;
            ZJC_DEBUG("init latest pool info shard: %u, pool %lu, init height: %lu",
                network_id, pool_index_, latest_height_);
        }
    }
}

void TxPool::UpdateSyncedHeight() {
    if (!height_tree_ptr_) {
        return;
    }

    for (; synced_height_ <= latest_height_; ++synced_height_) {
        if (!height_tree_ptr_->Valid(synced_height_ + 1)) {
            break;
        }
    }
}

uint64_t TxPool::UpdateLatestInfo(
        uint64_t height,
        const std::string& hash,
        const std::string& prehash,
        const uint64_t timestamp) {
    CheckThreadIdValid();
    auto tmp_height_tree_ptr = height_tree_ptr_;
    if (!tmp_height_tree_ptr) {
        InitHeightTree();
    }

    tmp_height_tree_ptr = height_tree_ptr_;
    if (tmp_height_tree_ptr) {
        ZJC_DEBUG("success set height, net: %u, pool: %u, height: %lu",
            common::GlobalInfo::Instance()->network_id(), pool_index_, height);
        tmp_height_tree_ptr->Set(height);
    }

    if (latest_height_ == common::kInvalidUint64 || latest_height_ < height) {
        latest_height_ = height;
        latest_hash_ = hash;
        latest_timestamp_ = timestamp;
    }

    if (to_sync_max_height_ == common::kInvalidUint64 || to_sync_max_height_ < latest_height_) {
        to_sync_max_height_ = latest_height_;
    }

    if (height > synced_height_) {
        checked_height_with_prehash_[height] = prehash;
        // CHECK_MEMORY_SIZE(checked_height_with_prehash_);
    }

    if (synced_height_ + 1 == height) {
        synced_height_ = height;
        auto iter = checked_height_with_prehash_.begin();
        while (iter != checked_height_with_prehash_.end()) {
            if (iter->first < synced_height_) {
                iter = checked_height_with_prehash_.erase(iter);
            } else {
                ++iter;
            }
        }

        UpdateSyncedHeight();
        if (prev_synced_height_ < synced_height_) {
            prev_synced_height_ = synced_height_;
        }
    } else {
        SyncBlock();
    }

    ZJC_DEBUG("pool index: %d, new height: %lu, new synced height: %lu, prev_synced_height_: %lu, to_sync_max_height_: %lu, latest height: %lu",
        pool_index_, height, synced_height_, prev_synced_height_, to_sync_max_height_, latest_height_);
    return synced_height_;
}

void TxPool::SyncBlock() {
    if (height_tree_ptr_ == nullptr) {
        return;
    }

    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
        net_id -= network::kConsensusWaitingShardOffset;
    }

    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    for (; prev_synced_height_ < to_sync_max_height_ &&
            (prev_synced_height_ < synced_height_ + 64);
            ++prev_synced_height_) {
        if (!height_tree_ptr_->Valid(prev_synced_height_ + 1)) {
            ZJC_DEBUG("now add sync height 1, %u_%u_%lu", 
                net_id,
                pool_index_,
                prev_synced_height_ + 1);
            kv_sync_->AddSyncHeight(
                net_id,
                pool_index_,
                prev_synced_height_ + 1,
                sync::kSyncHighest);
        }
    }
}

void TxPool::ConsensusAddTxs(const pools::TxItemPtr& tx_ptr) {
    if (!IsUserTransaction(tx_ptr->tx_info->step())) {
        return;
    }

    if (!account_tx_qps_check_.check(tx_ptr->tx_info->pubkey())) {
        ZJC_DEBUG("pools check firewall message failed, invalid qps limit pk: %s", 
            common::Encode::HexEncode(tx_ptr->tx_info->pubkey()).c_str());
        return;
    }
    
    CheckThreadIdValid();
    if (consensus_added_txs_.size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        ZJC_WARN("add failed extend %u, %u, all valid: %u", 
            consensus_added_txs_.size(), common::GlobalInfo::Instance()->each_tx_pool_max_txs(), all_tx_size());
        return;
    }

    if (tx_ptr->tx_key.empty()) {
        ZJC_WARN("add failed unique hash empty: %d", tx_ptr->tx_info->step());
        tx_ptr->tx_key = pools::GetTxMessageHash(*tx_ptr->tx_info);
    }

    consensus_added_txs_.push(tx_ptr);
}

}  // namespace pools

}  // namespace shardora
