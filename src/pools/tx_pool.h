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

struct TxItemPriOper {
    bool operator() (TxItemPtr& a, TxItemPtr& b) {
        return a->gas_price < b->gas_price;
    }
};

class TxPool {
public:
    TxPool();
    ~TxPool();
    void Init(
        uint32_t pool_idx,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync);
    int AddTx(TxItemPtr& tx_ptr);
    void GetTx(std::map<std::string, TxItemPtr>& res_map, uint32_t count);
    void TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list);
    void TxRecover(std::map<std::string, TxItemPtr>& txs);
    void CheckTimeoutTx();
    uint32_t SyncMissingBlocks(uint8_t thread_idx, uint64_t now_tm_ms);
    void RemoveTx(const std::string& gid);

    void FlushHeightTree(db::DbWriteBatch& db_batch) {
        if (height_tree_ptr_ != nullptr) {
            height_tree_ptr_->FlushToDb(db_batch);
        }
    }

    std::shared_ptr<consensus::WaitingTxsItem> GetTx(
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
            std::vector<uint8_t>* invalid_txs) {
        auto txs_items = std::make_shared<consensus::WaitingTxsItem>();
        auto& tx_map = txs_items->txs;
        for (int32_t i = 0; i < tx_hash_list.size(); ++i) {
            auto& txhash = tx_hash_list[i];
            auto iter = gid_map_.find(txhash);
            if (iter == gid_map_.end()) {
                ZJC_INFO("failed get tx %u, %s", pool_index_, common::Encode::HexEncode(txhash).c_str());
                if (invalid_txs == nullptr) {
                    return nullptr;
                }

                invalid_txs->push_back(i);
                continue;
            }

            if (invalid_txs != nullptr && !invalid_txs->empty()) {
                continue;
            }

            ZJC_DEBUG("success get tx %u, %s", pool_index_, common::Encode::HexEncode(txhash).c_str());
            tx_map[txhash] = iter->second;
        }
        
        if (invalid_txs != nullptr && !invalid_txs->empty()) {
            return nullptr;
        }

        return txs_items;
    }

    uint64_t latest_height() {
        if (latest_height_ == common::kInvalidUint64) {
            InitLatestInfo();
        }

//         assert(latest_height_ != common::kInvalidUint64);
        return latest_height_;
    }

    std::string& latest_hash() {
        if (latest_hash_.empty()) {
            InitLatestInfo();
        }

//         assert(!latest_hash_.empty());
        return latest_hash_;
    }

    void GetHeightInvalidChangeLeaderHashs(uint64_t height, std::vector<std::string>&hashs) {
        auto iter = change_leader_invalid_hashs_.find(height);
        if (iter == change_leader_invalid_hashs_.end()) {
            return;
        }

        for (auto hiter = iter->second.begin(); hiter != iter->second.end(); ++hiter) {
            hashs.push_back(*hiter);
            ZJC_DEBUG("success get invalid hash pool: %u, height: %lu, hash: %s",
                pool_index_, height, common::Encode::HexEncode(*hiter).c_str());
        }
    }

    void AddChangeLeaderInvalidHash(uint64_t height, const std::string& hash) {
        if (height != latest_height_) {
            return;
        }

        auto iter = change_leader_invalid_hashs_.find(height);
        if (iter == change_leader_invalid_hashs_.end()) {
            change_leader_invalid_hashs_[height] = std::set<std::string>();
            iter = change_leader_invalid_hashs_.find(height);
        } else {
            auto exists_iter = iter->second.find(hash);
            if (exists_iter != iter->second.end()) {
                return;
            }
        }

        ZJC_DEBUG("success add invalid hash pool: %u, height: %lu, hash: %s",
            pool_index_, height, common::Encode::HexEncode(hash).c_str());
        iter->second.insert(hash);
        SaveTempBftInvalidHashs(height, iter->second);
    }

    void SaveTempBftInvalidHashs(uint64_t height, const std::set<std::string>& hashs) {
        uint32_t network_id = common::GlobalInfo::Instance()->network_id();
        if (network_id == common::kInvalidUint32) {
            return;
        }

        if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
            network_id < network::kConsensusWaitingShardEndNetworkId) {
            network_id -= network::kConsensusWaitingShardOffset;
        }
        
        prefix_db_->SaveTempHeightInvalidHashs(network_id, pool_index_, height, hashs);
    }

    void InitGetTempBftInvalidHashs() {
        uint32_t network_id = common::GlobalInfo::Instance()->network_id();
        if (network_id == common::kInvalidUint32) {
            return;
        }

        if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
            network_id < network::kConsensusWaitingShardEndNetworkId) {
            network_id -= network::kConsensusWaitingShardOffset;
        }

        std::set<std::string> hashs;
        prefix_db_->GetTempHeightInvalidHashs(network_id, pool_index_, latest_height_ + 1, &hashs);
        if (!hashs.empty()) {
            change_leader_invalid_hashs_[latest_height_ + 1] = hashs;
        }
    }

    uint64_t UpdateLatestInfo(
            uint8_t thread_idx,
            uint64_t height,
            const std::string& hash,
            const std::string& prehash) {
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
            InitGetTempBftInvalidHashs();
        }

        if (to_sync_max_height_ == common::kInvalidUint64 || to_sync_max_height_ < latest_height_) {
            to_sync_max_height_ = latest_height_;
        }

        if (height > synced_height_) {
            checked_height_with_prehash_[height] = prehash;
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
            SyncBlock(thread_idx);
        }

        ZJC_DEBUG("pool index: %d, new height: %lu, new synced height: %lu, prev_synced_height_: %lu, to_sync_max_height_: %lu, latest height: %lu",
            pool_index_, height, synced_height_, prev_synced_height_, to_sync_max_height_, latest_height_);
        return synced_height_;
    }

    bool is_next_block_checked(uint64_t height, const std::string& hash) {
        auto iter = checked_height_with_prehash_.find(height + 1);
        if (iter != checked_height_with_prehash_.end()) {
            return iter->second == hash;
        }

        return false;
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

    double CheckLeaderValid(bool get_factor, uint32_t* finished_count, uint32_t* tx_count) {
        all_finish_tx_count_ += finish_tx_count_;
        double factor = 0.0;
        if (get_factor) {
            if (all_tx_count_ > 0) {
                factor = (double)all_finish_tx_count_ / (double)all_tx_count_;
            } else {
                factor = 1.0;
            }

            *finished_count = all_finish_tx_count_;
            *tx_count = all_tx_count_;
            all_tx_count_ = 0;
            all_finish_tx_count_ = 0;
        }

        all_tx_count_ += gid_map_.size();
        finish_tx_count_ = 0;
        return factor;
    }

private:
    void GetTx(
        std::map<std::string, TxItemPtr>& src_prio_map,
        std::map<std::string, TxItemPtr>& res_map,
        uint32_t count);
    void InitHeightTree();
    void InitLatestInfo() {
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
            if (latest_height_ == common::kInvalidUint64 || latest_height_ < pool_info.height()) {
                latest_height_ = pool_info.height();
                latest_hash_ = pool_info.hash();
                synced_height_ = pool_info.synced_height();
                prev_synced_height_ = synced_height_;
                to_sync_max_height_ = latest_height_;
                InitGetTempBftInvalidHashs();
                ZJC_DEBUG("init latest pool info shard: %u, pool %lu, init height: %lu",
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

    std::unordered_map<std::string, TxItemPtr> gid_map_;
    std::queue<std::string> timeout_txs_;
    std::queue<std::string> timeout_remove_txs_;
    common::LimitHashSet<std::string> removed_gid_{ 10240 };
    std::map<std::string, TxItemPtr> prio_map_;
    std::map<std::string, TxItemPtr> universal_prio_map_;
    uint64_t latest_height_ = common::kInvalidUint64;
    std::string latest_hash_;
    std::unordered_map<uint64_t, std::set<std::string>> change_leader_invalid_hashs_;
    std::shared_ptr<HeightTreeLevel> height_tree_ptr_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    uint64_t synced_height_ = 0;
    uint64_t prev_synced_height_ = 0;
    uint64_t to_sync_max_height_ = 0;
    uint64_t prev_synced_time_ms_ = 0;
    std::shared_ptr<db::Db> db_ = nullptr;
    uint32_t all_finish_tx_count_ = 0;
    uint32_t all_tx_count_ = 0;
    uint32_t checked_count_ = 0;
    volatile uint32_t finish_tx_count_ = 0;
    std::map<uint64_t, std::string> checked_height_with_prehash_;

    DISALLOW_COPY_AND_ASSIGN(TxPool);
};

}  // namespace pools

}  // namespace zjchain
