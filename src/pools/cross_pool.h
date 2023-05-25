#pragma once

#include <memory>
#include <map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <set>
#include <deque>
#include <queue>

#include "common/bloom_filter.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/spin_mutex.h"
#include "common/time_utils.h"
#include "common/limit_hash_set.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"
#include "network/network_utils.h"
#include "pools/tx_utils.h"
#include "protos/pools.pb.h"
#include "pools/height_tree_level.h"
#include "sync/key_value_sync.h"

namespace zjchain {

namespace pools {

class CrossPool {
public:
    CrossPool();
    ~CrossPool();
    void Init(
        uint32_t des_sharding_id,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync);
    uint32_t SyncMissingBlocks(uint8_t thread_idx, uint64_t now_tm_ms);

    void FlushHeightTree() {
        if (height_tree_ptr_ != nullptr) {
            height_tree_ptr_->FlushToDb();
        }
    }

    uint64_t latest_height() const {
        return latest_height_;
    }

    std::string latest_hash() const {
        return latest_hash_;
    }

    void UpdateToSyncHeight(uint8_t thread_idx, uint64_t to_sync_max_height) {
        if (to_sync_max_height_ < to_sync_max_height) {
            to_sync_max_height_ = to_sync_max_height;
            SyncBlock(thread_idx);
        }
    }

    uint64_t UpdateLatestInfo(uint8_t thread_idx, uint64_t height, const std::string& hash) {
        if (height_tree_ptr_ == nullptr) {
            InitHeightTree();
        }

        if (height_tree_ptr_ != nullptr) {
            height_tree_ptr_->Set(height);
            ZJC_DEBUG("success set height, net: %u, pool: %u, height: %lu",
                common::GlobalInfo::Instance()->network_id(), pool_index_, height);
        }

        if (latest_height_ == common::kInvalidUint64 || latest_height_ < height) {
            latest_height_ = height;
            latest_hash_ = hash;
            
        }

        if (to_sync_max_height_ == common::kInvalidUint64 || to_sync_max_height_ < latest_height_) {
            to_sync_max_height_ = latest_height_;
        }

        if (synced_height_ + 1 == height) {
            synced_height_ = height;
            UpdateSyncedHeight();
            if (prev_synced_height_ < synced_height_) {
                prev_synced_height_ = synced_height_;
            }
        } else {
            SyncBlock(thread_idx);
        }

        ZJC_INFO("pool index: %d, new height: %lu, new synced height: %lu, prev_synced_height_: %lu, to_sync_max_height_: %lu, latest height: %lu",
            pool_index_, height, synced_height_, prev_synced_height_, to_sync_max_height_, latest_height_);
        return synced_height_;
    }

    void SyncBlock(uint8_t thread_idx) {
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
                kv_sync_->AddSyncHeight(
                    thread_idx,
                    net_id,
                    pool_index_,
                    prev_synced_height_ + 1,
                    sync::kSyncHighest);
            }
        }
    }

private:
    void InitHeightTree();
    void InitLatestInfo() {
        pools::protobuf::PoolLatestInfo pool_info;
        uint32_t network_id = des_sharding_id_;
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
            if (latest_height_ == common::kInvalidUint64 || latest_height_ < pool_info.height()) {
                latest_height_ = pool_info.height();
                latest_hash_ = pool_info.hash();
                synced_height_ = pool_info.synced_height();
                prev_synced_height_ = synced_height_;
                to_sync_max_height_ = latest_height_;
                ZJC_DEBUG("init height tree latest info network: %u, pool %lu, init height: %lu",
                    network_id, pool_index_, latest_height_);
            }
        }
    }

    void UpdateSyncedHeight() {
        if (height_tree_ptr_ == nullptr) {
            return;
        }

        for (; synced_height_ <= latest_height_; ++synced_height_) {
            if (!height_tree_ptr_->Valid(synced_height_ + 1)) {
                break;
            }
        }
    }

    static const uint64_t kSyncBlockPeriodMs = 3000lu;

    uint32_t des_sharding_id_ = common::kInvalidUint32;
    uint64_t latest_height_ = common::kInvalidUint64;
    std::string latest_hash_;
    std::shared_ptr<HeightTreeLevel> height_tree_ptr_ = nullptr;
    uint32_t pool_index_ = common::kRootChainPoolIndex;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    uint64_t synced_height_ = 0;
    uint64_t prev_synced_height_ = 0;
    uint64_t to_sync_max_height_ = 0;
    uint64_t prev_synced_time_ms_ = 0;
    
std::shared_ptr<db::Db> db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(CrossPool);
};

}  // namespace pools

}  // namespace zjchain
