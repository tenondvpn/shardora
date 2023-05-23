#include "pools/tx_pool.h"
#include <cassert>

#include "common/encode.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/tx_utils.h"

namespace zjchain {

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

uint32_t TxPool::SyncMissingBlocks(uint8_t thread_idx, uint64_t now_tm_ms) {
    if (height_tree_ptr_ == nullptr) {
        return 0;
    }

    if (prev_synced_time_ms_ >= now_tm_ms) {
        return 0;
    }

    if (latest_height_ == common::kInvalidUint64) {
        // sync latest height from neighbors
        return 0;
    }

    prev_synced_time_ms_ = now_tm_ms + kSyncBlockPeriodMs;
    std::vector<uint64_t> invalid_heights;
    height_tree_ptr_->GetMissingHeights(&invalid_heights, latest_height_);
    if (invalid_heights.size() > 0) {
        ZJC_DEBUG("pool: %u, sync missing blocks latest height: %lu, invaid heights size: %u, height: %lu",
            pool_index_, latest_height_, invalid_heights.size(), invalid_heights[0]);
        auto net_id = common::GlobalInfo::Instance()->network_id();
        if (net_id >= network::kConsensusWaitingShardBeginNetworkId &&
            net_id < network::kConsensusWaitingShardEndNetworkId) {
            net_id -= network::kConsensusWaitingShardOffset;
        }

        if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
            return 0;
        }

        for (uint32_t i = 0; i < invalid_heights.size(); ++i) {
            kv_sync_->AddSyncHeight(
                thread_idx,
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
    if (removed_gid_.DataExists(tx_ptr->gid)) {
        return kPoolsTxAdded;
    }

    assert(tx_ptr != nullptr);
    auto iter = gid_map_.find(tx_ptr->tx_hash);
    if (iter != gid_map_.end()) {
        return kPoolsTxAdded;
    }

    if (tx_ptr->step == pools::protobuf::kCreateLibrary) {
        universal_prio_map_[tx_ptr->prio_key] = tx_ptr;
    } else {
        prio_map_[tx_ptr->prio_key] = tx_ptr;
    }

    gid_map_[tx_ptr->gid] = tx_ptr;
    timeout_txs_.push(tx_ptr->gid);
    return kPoolsSuccess;
}

void TxPool::GetTx(std::map<std::string, TxItemPtr>& res_map, uint32_t count) {
    GetTx(universal_prio_map_, res_map, count);
    if (!res_map.empty()) {
        return;
    }

    GetTx(prio_map_, res_map, count);
}

void TxPool::GetTx(
        std::map<std::string, TxItemPtr>& src_prio_map,
        std::map<std::string, TxItemPtr>& res_map,
        uint32_t count) {
    auto timestamp_now = common::TimeUtils::TimestampUs();
    std::vector<TxItemPtr> recover_txs;
    auto iter = src_prio_map.begin();
    while (iter != src_prio_map.end()) {
        if (iter->second->time_valid >= timestamp_now) {
            break;
        }

        res_map[iter->second->tx_hash] = iter->second;
        ZJC_DEBUG("success get local transfer to tx %u, %s",
            pool_index_, common::Encode::HexEncode(iter->second->tx_hash).c_str());
        src_prio_map.erase(iter++);
        if (res_map.size() >= count) {
            return;
        }
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
            common::Encode::HexEncode(iter->second->tx_hash).c_str());
    }
}

void TxPool::TxRecover(std::map<std::string, TxItemPtr>& txs) {
    for (auto iter = txs.begin(); iter != txs.end(); ++iter) {
        iter->second->in_consensus = false;
        auto miter = gid_map_.find(iter->first);
        if (miter != gid_map_.end()) {
            if (iter->second->step == pools::protobuf::kCreateLibrary) {
                universal_prio_map_[miter->second->prio_key] = miter->second;
            } else {
                prio_map_[miter->second->prio_key] = miter->second;
            }
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

    gid_map_.erase(giter);
    ZJC_DEBUG("remove tx success gid: %s, tx hash: %s",
        common::Encode::HexEncode(giter->second->gid).c_str(),
        common::Encode::HexEncode(giter->second->tx_hash).c_str());
}

void TxPool::TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list) {
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        RemoveTx(tx_list[i].gid());
    }

    finish_tx_count += tx_list.size();
}

}  // namespace pools

}  // namespace zjchain
