#include "pools/tx_pool.h"
#include <cassert>
#include <common/log.h>

#include "common/encode.h"
#include "common/utils.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_utils.h"

namespace shardora {

namespace pools {
    
TxPool::TxPool() {}

TxPool::~TxPool() {}

void TxPool::Init(
        uint32_t pool_idx,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync) {
    kv_sync_ = kv_sync;
    pool_index_ = pool_idx;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    InitLatestInfo();
    InitHeightTree();
}

void TxPool::InitHeightTree() {
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

    height_tree_ptr_ = std::make_shared<HeightTreeLevel>(
        net_id,
        pool_index_,
        latest_height_,
        db_);
    height_tree_ptr_->Set(0);
    for (; synced_height_ <= latest_height_; ++synced_height_) {
        if (!height_tree_ptr_->Valid(synced_height_ + 1)) {
            break;
        }
    }
}

uint32_t TxPool::SyncMissingBlocks(uint64_t now_tm_ms) {
    if (height_tree_ptr_ == nullptr) {
        return 0;
    }

//     if (prev_synced_time_ms_ >= now_tm_ms) {
//         return 0;
//     }
// 
    if (latest_height_ == common::kInvalidUint64) {
        // sync latest height from neighbors
        return 0;
    }

//     prev_synced_time_ms_ = now_tm_ms + kSyncBlockPeriodMs;
    std::vector<uint64_t> invalid_heights;
    height_tree_ptr_->GetMissingHeights(&invalid_heights, latest_height_);
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
//     common::AutoSpinLock auto_lock(mutex_);
    if (removed_gid_.DataExists(tx_ptr->tx_info.gid())) {
        return kPoolsTxAdded;
    }

    if (gid_map_.size() >= common::GlobalInfo::Instance()->each_tx_pool_max_txs()) {
        // ZJC_WARN("add failed extend 1024");
        return kPoolsError;
    }

    assert(tx_ptr != nullptr);
    auto iter = gid_map_.find(tx_ptr->tx_info.gid());
    if (iter != gid_map_.end()) {
        return kPoolsTxAdded;
    }

    if (tx_ptr->step == pools::protobuf::kCreateLibrary) {
        universal_prio_map_[tx_ptr->prio_key] = tx_ptr;
    } else {
        prio_map_[tx_ptr->prio_key] = tx_ptr;
    }

    gid_map_[tx_ptr->tx_info.gid()] = tx_ptr;
#ifdef LATENCY
    auto now_tm_us = common::TimeUtils::TimestampUs();
    if (prev_tx_count_tm_us_ == 0) {
        prev_tx_count_tm_us_ = now_tm_us;
    }

    if (now_tm_us > prev_tx_count_tm_us_ + 3000000lu) {
        ZJC_INFO("waiting_tx_count pool: %d: tx: %llu", pool_index_, gid_map_.size());
        prev_tx_count_tm_us_ = now_tm_us;
    }

    gid_start_time_map_[tx_ptr->tx_info.gid()] = common::TimeUtils::TimestampUs(); 
    oldest_timestamp_ = prio_map_.begin()->second->time_valid;
#endif
    timeout_txs_.push(tx_ptr->tx_info.gid());
    return kPoolsSuccess;
}

void TxPool::GetTx(
        const std::map<std::string, pools::TxItemPtr>& invalid_txs, 
        zbft::protobuf::TxBft* txbft, 
        uint32_t count) {
    std::vector<TxItemPtr> recover_txs;
    auto iter = prio_map_.begin();
    while (iter != prio_map_.end() && txbft->txs_size() < count) {
        auto invalid_iter = invalid_txs.find(iter->second->unique_tx_hash);
        if (invalid_iter != invalid_txs.end()) {
            ++iter;
            continue;
        }

        auto* tx = txbft->add_txs();
        *tx = iter->second->tx_info;
        ZJC_DEBUG("backup success get local transfer to tx %u, %s, step: %d",
            pool_index_, 
            common::Encode::HexEncode(iter->second->unique_tx_hash).c_str(),
            iter->second->tx_info.step());
        assert(!iter->second->unique_tx_hash.empty());
        ++iter;
    }
}

void TxPool::GetTx(std::map<std::string, TxItemPtr>& res_map, uint32_t count) {
    GetTx(universal_prio_map_, res_map, count);
    if (!res_map.empty()) {
        return;
    }

    GetTx(prio_map_, res_map, count);
    GetTx(consensus_tx_map_, res_map, count);
}

void TxPool::GetTx(
        std::map<std::string, TxItemPtr>& src_prio_map,
        std::map<std::string, TxItemPtr>& res_map,
        uint32_t count) {
    auto timestamp_now = common::TimeUtils::TimestampUs();
    std::vector<TxItemPtr> recover_txs;
    auto iter = src_prio_map.begin();
    while (iter != src_prio_map.end() && res_map.size() < count) {
        res_map[iter->second->unique_tx_hash] = iter->second;
        ZJC_DEBUG("leader success get local transfer to tx %u, %s, step: %d",
            pool_index_, 
            common::Encode::HexEncode(iter->second->unique_tx_hash).c_str(),
            iter->second->tx_info.step());
        assert(!iter->second->unique_tx_hash.empty());
        iter = src_prio_map.erase(iter);
    }
}

void TxPool::CheckTimeoutTx() {
//     common::AutoSpinLock auto_lock(mutex_);
    auto now_tm = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    while (!timeout_txs_.empty() && count++ < 64) {
        auto& gid = timeout_txs_.front();
        auto iter = gid_map_.find(gid);
        if (iter == gid_map_.end()) {
            timeout_txs_.pop();
            continue;
        }

        if (iter->second->timeout > now_tm) {
            break;
        }

        timeout_txs_.pop();
        timeout_remove_txs_.push(gid);
    }

    count = 0;
    while (!timeout_remove_txs_.empty() && count++ < 64) {
        auto& gid = timeout_remove_txs_.front();
        auto iter = gid_map_.find(gid);
        if (iter == gid_map_.end()) {
            timeout_remove_txs_.pop();
            continue;
        }

        if (iter->second->remove_timeout > now_tm) {
            break;
        }

        RemoveTx(gid);
        timeout_remove_txs_.pop();
        ZJC_DEBUG("timeout remove gid: %s, tx hash: %s",
            common::Encode::HexEncode(gid).c_str(),
            common::Encode::HexEncode(iter->second->unique_tx_hash).c_str());
    }
}

void TxPool::TxRecover(std::map<std::string, TxItemPtr>& txs) {
    for (auto iter = txs.begin(); iter != txs.end(); ++iter) {
        iter->second->in_consensus = false;
        auto miter = gid_map_.find(iter->first);
        if (miter != gid_map_.end()) {
            if (miter->second->is_consensus_add_tx) {
                consensus_tx_map_[miter->second->unique_tx_hash] = miter->second;
                continue;
            }

            if (iter->second->step == pools::protobuf::kCreateLibrary) {
                universal_prio_map_[miter->second->prio_key] = miter->second;
            } else {
                prio_map_[miter->second->prio_key] = miter->second;
            }
        }
    }
}

void TxPool::RecoverTx(const std::string& gid) {
    auto miter = gid_map_.find(gid);
    if (miter != gid_map_.end()) {
        if (miter->second->is_consensus_add_tx) {
            consensus_tx_map_[miter->second->unique_tx_hash] = miter->second;
            return;
        }
        if (miter->second->step == pools::protobuf::kCreateLibrary) {
            universal_prio_map_[miter->second->prio_key] = miter->second;
        } else {
            prio_map_[miter->second->prio_key] = miter->second;
        }        
    }
}

void TxPool::RemoveTx(const std::string& gid) {
    removed_gid_.Push(gid);
    auto giter = gid_map_.find(gid);
    if (giter == gid_map_.end()) {
        return;
    }

    giter->second->in_consensus = false;
    auto prio_iter = prio_map_.find(giter->second->prio_key);
    if (prio_iter != prio_map_.end()) {
        prio_map_.erase(prio_iter);
    }

    auto universal_prio_iter = universal_prio_map_.find(giter->second->prio_key);
    if (universal_prio_iter != universal_prio_map_.end()) {
        universal_prio_map_.erase(universal_prio_iter);
    }

    auto cons_iter = consensus_tx_map_.find(giter->second->unique_tx_hash);
    if (cons_iter != consensus_tx_map_.end()) {
        consensus_tx_map_.erase(cons_iter);
    }

//     ZJC_DEBUG("remove tx success gid: %s, tx hash: %s",
//         common::Encode::HexEncode(giter->second->gid).c_str(),
//         common::Encode::HexEncode(giter->second->tx_hash).c_str());
    gid_map_.erase(giter);
#ifdef LATENCY    
    if (!prio_map_.empty()) {
        oldest_timestamp_ = prio_map_.begin()->second->time_valid;
    } else {
        oldest_timestamp_ = 0;
    }
#endif
}

void TxPool::TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list) {
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        auto& gid = tx_list[i].gid(); 
        RemoveTx(gid);
#ifdef LATENCY
        // 统计交易确认延迟
        auto now_tm = common::TimeUtils::TimestampUs();
        auto start_tm_iter = gid_start_time_map_.find(gid);
        if (start_tm_iter != gid_start_time_map_.end()) {
            latencys_us_.push_back(now_tm - start_tm_iter->second);
            // ZJC_INFO("tx latency gid: %s, us: %llu",
            //     common::Encode::HexEncode(gid).c_str(), now_tm - start_tm_iter->second);
            gid_start_time_map_.erase(gid);
        }

        if (latencys_us_.size() > 10) {
            uint64_t p50 = common::GetNthElement(latencys_us_, 0.5);
            latencys_us_.clear();
        
            ZJC_INFO("tx latency p50: %llu", p50);
        }
#endif
    }

    finish_tx_count_ += tx_list.size();
}

}  // namespace pools

}  // namespace shardora
