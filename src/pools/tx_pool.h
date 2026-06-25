#pragma once

#include <memory>
#include <map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <unordered_set>
#include <vector>
#include <queue>

#include "common/bloom_filter.h"
#include "common/global_info.h"
#include "common/lru_map.h"
#include "common/hash.h"
#include "common/spin_mutex.h"
#include "common/time_utils.h"
#include "common/limit_hash_set.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"
#include "network/network_utils.h"
#include "pools/account_qps_lru_map.h"
#include "pools/tx_utils.h"
#include "pools/unique_hash_lru_set.h"
#include "protos/pools.pb.h"
#include "pools/height_tree_level.h"
#include "sync/key_value_sync.h"

namespace shardora {

namespace pools {

class TxPoolManager;
struct TxItemPriOper {
    bool operator() (TxItemPtr& a, TxItemPtr& b) {
        return a->tx_info->gas_price() < b->tx_info->gas_price();
    }
};

class TxPool {
public:
    TxPool();
    ~TxPool();
    void Init(
        TxPoolManager* pools_mgr,
        std::shared_ptr<security::Security> security,
        uint32_t pool_idx,
        const std::shared_ptr<db::Db>& db,
        std::shared_ptr<sync::KeyValueSync>& kv_sync);
    int AddTx(TxItemPtr& tx_ptr);
    void GetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::vector<pools::TxItemPtr>& res_map, 
        uint32_t count, 
        pools::CheckAddrNonceValidFunction tx_valid_func);
    void GetTxSyncToLeader(
        uint32_t leader_idx, 
        uint32_t count,
        ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
        pools::CheckAddrNonceValidFunction tx_valid_func,
        const std::unordered_map<std::string, uint64_t>& leader_nonce_map);
    uint32_t SyncMissingBlocks(uint64_t now_tm_ms);
    void ConsensusAddTxs(const pools::TxItemPtr& tx);
    uint64_t UpdateLatestInfo(
            uint64_t height,
            const std::string& hash,
            const std::string& prehash,
            const uint64_t timestamp);
    void SyncBlock();
    void TxOver(view_block::protobuf::ViewBlockItem& view_block);

    void OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height) {
        if (!network::IsSameToLocalShard(sharding_id)) {
            return;
        }

        if (elect_height > latest_elect_height_) {
            latest_elect_height_ = elect_height;
        }
    }

    bool PoolChainIsFull(uint64_t height) const {
        if (latest_height_ < height) {
            SHARDORA_DEBUG("pool: %d, check pool chain is full height: %lu, latest_height_: %lu", 
                pool_index_, height, latest_height_);
            return false;
        }

        return !has_missing_height_ ;
    }

    bool TxKeyExists(const std::string& addr, uint64_t nonce, const std::string& key) {
        auto iter = tx_map_.find(addr);
        if (iter != tx_map_.end()) {
            auto nonce_iter = iter->second.find(nonce);
            if (nonce_iter != iter->second.end()) {
                SHARDORA_DEBUG("pool: %d, check tx key exists addr: %s, nonce: %lu, key: %s, exist key: %s", 
                    pool_index_,
                    common::Encode::HexEncode(addr).c_str(),
                    nonce,
                    common::Encode::HexEncode(key).c_str(),
                    common::Encode::HexEncode(nonce_iter->second->tx_info->key()).c_str());
                if (nonce_iter->second->tx_info->key() == key) {
                    return true;
                } else {
                    SHARDORA_DEBUG("pool: %d, check tx key not exist addr: %s, nonce: %lu, key: %s, exist key: %s", 
                        pool_index_,
                        common::Encode::HexEncode(addr).c_str(),
                        nonce,
                        common::Encode::HexEncode(key).c_str(),
                        common::Encode::HexEncode(nonce_iter->second->tx_info->key()).c_str());
                }
            } else {
                SHARDORA_DEBUG("pool: %d, check tx key not exist addr: %s, nonce: %lu, key: %s", 
                    pool_index_,
                    common::Encode::HexEncode(addr).c_str(),
                    nonce,
                    common::Encode::HexEncode(key).c_str());
            }
        } else {
            SHARDORA_DEBUG("pool: %d, check tx key not exist addr: %s, nonce: %lu, key: %s", 
                    pool_index_,
                    common::Encode::HexEncode(addr).c_str(),
                    nonce,
                    common::Encode::HexEncode(key).c_str());
        }

        return false;
    }

    bool NewTxValid(const std::string& addr, uint64_t nonce) {
        std::shared_ptr<std::unordered_map<std::string, uint64_t>> over_map_ptr;
        while (over_addr_map_queue_.pop(&over_map_ptr) && over_map_ptr != nullptr) {
            for (auto iter = over_map_ptr->begin(); iter != over_map_ptr->end(); ++iter) {
                add_addr_nonce_map_.Put(iter->first, iter->second);
            }
        }

        uint64_t over_nonce = 0lu;
        if (add_addr_nonce_map_.Get(addr, over_nonce)) {
            if (over_nonce >= nonce || (over_nonce + 4 * common::kMaxTxCount) <= nonce) {
                SHARDORA_DEBUG("trace tx pool: %d, failed add tx %s, nonce: %lu, over_nonce: %lu, max nonce: %lu", 
                    pool_index_,
                    common::Encode::HexEncode(addr).c_str(),
                    nonce,
                    over_nonce,
                    (over_nonce + 4 * common::kMaxTxCount));
                return false;
            }
        }

        return true;
    }

    uint32_t all_tx_size() const {
        uint32_t cons_map_size = 0;
        for (auto iter = consensus_tx_map_.begin(); iter != consensus_tx_map_.end(); ++iter) {
            cons_map_size = iter->second.size();
        }

        return added_txs_.size() + 
            consensus_added_txs_.size() + 
            cons_map_size;
    }
    
    uint64_t oldest_timestamp() const {
        return oldest_timestamp_;
    }

    void FlushHeightTree(db::DbWriteBatch& db_batch) {
        // CheckThreadIdValid();
        if (height_tree_ptr_ != nullptr) {
            auto tmp_tree_ptr = height_tree_ptr_;
            std::vector<uint64_t> invalid_heights;
            tmp_tree_ptr->GetMissingHeights(&invalid_heights, latest_height_);
            SHARDORA_DEBUG("%u get invalid heights size: %u, latest_height_: %lu", 
                pool_index_, invalid_heights.size(), latest_height_);
            if (invalid_heights.size() > 0 && invalid_heights[0] <= latest_height_) {
                has_missing_height_ = true;
            } else {
                has_missing_height_ = false;
            }

            tmp_tree_ptr->FlushToDb(db_batch);
            height_tree_ptr_ = nullptr;
        }
    }

    uint64_t latest_height() {
        if (latest_height_ == common::kInvalidUint64) {
            InitLatestInfo();
        }

//         //assert(latest_height_ != common::kInvalidUint64);
        return latest_height_;
    }

    std::string& latest_hash() {
        if (latest_hash_.empty()) {
            InitLatestInfo();
        }

//         //assert(!latest_hash_.empty());
        return latest_hash_;
    }
    
    uint64_t latest_timestamp() {
        if (latest_timestamp_ == 0) {
            InitLatestInfo();
        }
        
        return latest_timestamp_;
    }

private:
    void InitHeightTree();
    void InitLatestInfo();
    void UpdateSyncedHeight();
    void TempGetTxIdempotently(
        transport::MessagePtr msg_ptr, 
        std::vector<pools::TxItemPtr>& res_map, 
        uint32_t count, 
        pools::CheckAddrNonceValidFunction tx_valid_func);
    void RecordNormalToDelay(uint64_t now_tm_us, const TxItemPtr& tx_ptr);
    void MaybeReportNormalToDelay(uint64_t now_tm_us);

    static const uint64_t kSyncBlockPeriodMs = 1000lu;
    static const uint64_t kUserPopedTxTimeoutSec = 10lu;
    static const uint64_t kSystemPopedTxTimeoutSec = 3lu;

    uint64_t latest_height_ = common::kInvalidUint64;
    std::string latest_hash_;
    uint64_t latest_timestamp_ = 0U;
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
    std::atomic<uint32_t> finish_tx_count_ = 0;
    std::map<uint64_t, std::string> checked_height_with_prehash_;
    std::atomic<uint64_t> oldest_timestamp_ = 0;
    uint64_t prev_tx_count_tm_us_ = 0;
    TxPoolManager* pools_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    uint64_t prev_check_tx_timeout_tm_ = 0;
    std::thread::id local_thread_id_;
    uint64_t local_thread_id_count_ = 0;
    common::ThreadSafeQueue<TxItemPtr, 1024 * 256> added_txs_;
    common::ThreadSafeQueue<TxItemPtr, 1024 * 256> consensus_added_txs_;
    common::ThreadSafeQueue<std::shared_ptr<std::unordered_map<std::string, uint64_t>>, 1024 * 256> over_addr_map_queue_;
    common::LRUMap<std::string, uint64_t> add_addr_nonce_map_{102400};
    std::map<std::string, std::map<uint64_t, TxItemPtr>> tx_map_;
    std::map<std::string, std::map<uint64_t, TxItemPtr>> consensus_tx_map_;
    uint32_t consensus_tx_map_count_ = 0;
    std::atomic<bool> has_missing_height_ = true;
    std::atomic<uint64_t> latest_elect_height_ = 0;
    // Dirty flag: set to true when new txs arrive or txs are committed (nonces advance).
    // When false AND both queues are empty, TempGetTxIdempotently can skip the
    // expensive full scan of tx_map_ / consensus_tx_map_.
    bool tx_pool_dirty_ = true;

// TODO: just test
    db::DbWriteBatch added_gids_batch_;
    uint64_t all_delay_tm_us_ = 0;
    uint64_t all_delay_tx_count_ = 0;
    uint64_t prev_delay_tm_timeout_ = 0;
    uint64_t normal_to_delay_tm_us_ = 0;
    uint64_t normal_to_delay_tx_count_ = 0;
    uint64_t prev_normal_to_delay_tm_timeout_ = 0;

    DISALLOW_COPY_AND_ASSIGN(TxPool);
};

}  // namespace pools

}  // namespace shardora
