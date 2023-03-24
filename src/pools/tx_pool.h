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
    void Init(uint32_t pool_idx, const std::shared_ptr<db::Db>& db);
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
        auto tmp_ptr = latest_item_;
        return tmp_ptr->latest_height;
    }

    std::string latest_hash() const {
        auto tmp_ptr = latest_item_;
        return tmp_ptr->latest_hash;
    }

    void UpdateLatestInfo(uint64_t height, const std::string& hash) {
        auto latest_item = std::make_shared<PoolLatestItem>();
        if (latest_item_ != nullptr) {
            auto tmp_ptr = latest_item_;
            latest_item->latest_height = tmp_ptr->latest_height;
            latest_item->consequent_to_height = tmp_ptr->consequent_to_height;
            latest_item->prev_to_height = tmp_ptr->prev_to_height;
        }

        if (latest_item->latest_height < height) {
            latest_item->latest_height = height;
            latest_item->latest_hash = hash;
            if (height_tree_ptr_ != nullptr) {
                height_tree_ptr_->Set(height);
            }

            if (height == latest_item->consequent_to_height + 1) {
                latest_item->consequent_to_height = height;
            } else {
                if (height_tree_ptr_ != nullptr) {
                    for (; latest_item->consequent_to_height < height; ++latest_item->consequent_to_height) {
                        if (!height_tree_ptr_->Valid(latest_item->consequent_to_height + 1)) {
                            break;
                        }
                    }
                }
            }

            latest_item_ = latest_item;
        }
    }

    void UpdateToHeight(uint64_t to_height) {
        auto latest_item = std::make_shared<PoolLatestItem>();
        if (latest_item_ != nullptr) {
            auto tmp_ptr = latest_item_;
            latest_item->latest_height = tmp_ptr->latest_height;
            latest_item->latest_hash = tmp_ptr->latest_hash;
            latest_item->consequent_to_height = tmp_ptr->consequent_to_height;
            latest_item->prev_to_height = tmp_ptr->prev_to_height;
        }

        if (latest_item->prev_to_height < to_height) {
            latest_item->prev_to_height = to_height;
        }

        if (latest_item->consequent_to_height < to_height) {
            latest_item->consequent_to_height = to_height;
        }

        latest_item_ = latest_item;
    }

private:
    bool CheckTimeoutTx(TxItemPtr& tx_ptr, uint64_t timestamp_now);
    void InitLatestInfo() {
        pools::protobuf::PoolLatestInfo pool_info;
        if (prefix_db_->GetLatestPoolInfo(
                common::GlobalInfo::Instance()->network_id(),
                pool_index_,
                &pool_info)) {
            UpdateLatestInfo(pool_info.height(), pool_info.hash());
        }
    }

    std::deque<TxItemPtr> timeout_txs_;
    std::unordered_map<std::string, TxItemPtr> added_tx_map_;
    std::unordered_map<std::string, TxItemPtr> gid_map_;
    std::map<std::string, TxItemPtr> prio_map_;
    std::string latest_hash_;
    std::shared_ptr<HeightTreeLevel> height_tree_ptr_ = nullptr;
    std::shared_ptr<PoolLatestItem> latest_item_ = nullptr;
    uint32_t pool_index_ = common::kInvalidPoolIndex;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(TxPool);
};

}  // namespace pools

}  // namespace zjchain
