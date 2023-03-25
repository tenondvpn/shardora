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
#include "common/user_property_key_define.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"
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

    inline TxItemPtr GetTx(const std::string& tx_hash) {
        auto iter = added_tx_map_.find(tx_hash);
        if (iter != added_tx_map_.end()) {
            //         ZJC_DEBUG("success get tx %u, %s", pool_index_, common::Encode::HexEncode(tx_hash).c_str());
            return iter->second;
        }

        //     ZJC_DEBUG("failed get tx %u, %s", pool_index_, common::Encode::HexEncode(tx_hash).c_str());
        return nullptr;
    }

    std::shared_ptr<consensus::WaitingTxsItem> GetTx(
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list) {
        auto txs_items = std::make_shared<consensus::WaitingTxsItem>();
        auto& tx_map = txs_items->txs;
        for (int32_t i = 0; i < tx_hash_list.size(); ++i) {
            auto& txhash = tx_hash_list[i];
            auto iter = added_tx_map_.find(txhash);
            if (iter != added_tx_map_.end()) {
                //         ZJC_DEBUG("success get tx %u, %s", pool_index_, common::Encode::HexEncode(tx_hash).c_str());
                tx_map[txhash] = iter->second;
            }
        }
        //     ZJC_DEBUG("failed get tx %u, %s", pool_index_, common::Encode::HexEncode(tx_hash).c_str());
        return txs_items;
    }

    void GetTx(
        const common::BloomFilter& bloom_filter,
        std::map<std::string, TxItemPtr>& res_map);
    void TxOver(std::map<std::string, TxItemPtr>& txs);
    void TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list);
    void TxRecover(std::map<std::string, TxItemPtr>& txs);
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
        height_tree_ptr_->Set(height);
        if (latest_height_ < height) {
            latest_height_ = height;
            latest_hash_ = hash;
            
        }

        if (to_sync_max_height_ < latest_height_) {
            to_sync_max_height_ = latest_height_;
        }

        if (synced_height_ + 1 == height) {
            synced_height_ = height;
            if (prev_synced_height_ < synced_height_) {
                prev_synced_height_ = synced_height_;
            }
        } else {
            SyncBlock(thread_idx);
        }

        return synced_height_;
    }

    void SyncBlock(uint8_t thread_idx) {
        for (; prev_synced_height_ < to_sync_max_height_ &&
                (prev_synced_height_ < synced_height_ + 64);
                ++prev_synced_height_) {
            if (!height_tree_ptr_->Valid(prev_synced_height_ + 1)) {
                kv_sync_->AddSyncHeight(
                    thread_idx,
                    common::GlobalInfo::Instance()->network_id(),
                    pool_index_,
                    prev_synced_height_ + 1,
                    sync::kSyncHighest);
            }
        }
    }

private:
    bool CheckTimeoutTx(TxItemPtr& tx_ptr, uint64_t timestamp_now);
    void InitLatestInfo() {
        pools::protobuf::PoolLatestInfo pool_info;
        if (prefix_db_->GetLatestPoolInfo(
                common::GlobalInfo::Instance()->network_id(),
                pool_index_,
                &pool_info)) {
            if (latest_height_ < pool_info.height()) {
                latest_height_ = pool_info.height();
                latest_hash_ = pool_info.hash();
                synced_height_ = pool_info.synced_height();
                prev_synced_height_ = synced_height_;
                to_sync_max_height_ = latest_height_;
            }
        }
    }

    std::deque<TxItemPtr> timeout_txs_;
    std::unordered_map<std::string, TxItemPtr> added_tx_map_;
    std::unordered_map<std::string, TxItemPtr> gid_map_;
    std::map<std::string, TxItemPtr> prio_map_;
    uint64_t latest_height_ = 0;
    std::string latest_hash_;
    std::shared_ptr<HeightTreeLevel> height_tree_ptr_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    uint64_t synced_height_ = 0;
    uint64_t prev_synced_height_ = 0;
    uint64_t to_sync_max_height_ = 0;

    DISALLOW_COPY_AND_ASSIGN(TxPool);
};

}  // namespace pools

}  // namespace zjchain
