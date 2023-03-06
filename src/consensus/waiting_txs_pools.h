#pragma once

#include "block/block_manager.h"
#include "consensus/waiting_txs.h"

namespace zjchain {

namespace consensus {

class WaitingTxsPools {
public:
    WaitingTxsPools(
            std::shared_ptr<pools::TxPoolManager>& pool_mgr,
            std::shared_ptr<block::BlockManager>& block_mgr)
            : pool_mgr_(pool_mgr), block_mgr_(block_mgr) {
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            wtxs[i].Init(i, pool_mgr);
        }
    }

    ~WaitingTxsPools() {}

    void TxOver(std::shared_ptr<WaitingTxsItem>& tx_ptr) {
        pool_mgr_->TxOver(tx_ptr->pool_index, tx_ptr->txs);
        auto& item_set = pipeline_pools_[tx_ptr->pool_index];
        for (auto set_iter = item_set.begin(); set_iter != item_set.end(); ++set_iter) {
            if (*set_iter == tx_ptr) {
                item_set.erase(set_iter);
                break;
            }
        }
    }

    void TxRecover(std::shared_ptr<WaitingTxsItem>& tx_ptr) {
        pool_mgr_->TxRecover(tx_ptr->pool_index, tx_ptr->txs);
        auto& item_set = pipeline_pools_[tx_ptr->pool_index];
        for (auto set_iter = item_set.begin(); set_iter != item_set.end(); ++set_iter) {
            if (*set_iter == tx_ptr) {
                item_set.erase(set_iter);
                break;
            }
        }
    }

    void UpdateLatestInfo(uint32_t pool_index, uint64_t height, const std::string& hash) {
        pool_mgr_->UpdateLatestInfo(pool_index, height, hash);
    }

    uint64_t latest_height(uint32_t pool_index) const {
        return pool_mgr_->latest_height(pool_index);
    }

    std::string latest_hash(uint32_t pool_index) const {
        return pool_mgr_->latest_hash(pool_index);
    }
    
    void LockPool(std::shared_ptr<WaitingTxsItem>& tx_ptr) {
        pipeline_pools_[tx_ptr->pool_index].insert(tx_ptr);
    }

    std::shared_ptr<WaitingTxsItem> LeaderGetValidTxs(bool direct, uint32_t pool_index) {
        auto txs_item = wtxs[pool_index].LeaderGetValidTxs(direct);
        if (txs_item != nullptr) {
            txs_item->pool_index = pool_index;
            auto& tx_map = txs_item->txs;
            assert(!tx_map.empty());
            uint32_t bitcount = ((kBitcountWithItemCount * tx_map.size()) / 64) * 64;
            if (((kBitcountWithItemCount * tx_map.size()) % 64) > 0) {
                bitcount += 64;
            }

            txs_item->bloom_filter = std::make_shared<common::BloomFilter>(bitcount, kHashCount);
            for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
                txs_item->bloom_filter->Add(common::Hash::Hash64(iter->first));
            }

            // pass by bloomfilter
            auto error_bloomfilter_txs = wtxs[pool_index].FollowerGetTxs(*txs_item->bloom_filter);
            if (error_bloomfilter_txs != nullptr) {
                for (auto iter = error_bloomfilter_txs->txs.begin();
                        iter != error_bloomfilter_txs->txs.end(); ++iter) {
                    auto fiter = tx_map.find(iter->first);
                    if (fiter != tx_map.end()) {
                        continue;
                    }

                    tx_map[iter->first] = iter->second;
                }
            }

            auto& all_txs_hash = txs_item->all_txs_hash;
            all_txs_hash.reserve(tx_map.size() * 32);
            for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
                all_txs_hash.append(iter->first);
            }

            all_txs_hash = common::Hash::keccak256(all_txs_hash);
        }

        if (txs_item == nullptr) {
            auto tx_ptr = block_mgr_->GetToTx(pool_index);
            if (tx_ptr != nullptr) {
                txs_item = std::make_shared<WaitingTxsItem>();
                txs_item->pool_index = pool_index;
                txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
                txs_item->tx_type = pools::protobuf::kNormalTo;
                ZJC_DEBUG("success get to tx.");
            }
        }
        
        if (txs_item != nullptr) {
            LeaderFilterInvalidTx(pool_index, txs_item->txs);
            if (txs_item->txs.empty()) {
                txs_item = nullptr;
            }
        }

        return txs_item;
    }

    std::shared_ptr<WaitingTxsItem> FollowerGetToTxs(
            uint32_t pool_index,
            const std::string& tx_hash,
            uint8_t thread_idx) {
        auto tx_ptr = block_mgr_->GetToTx(pool_index);
        if (tx_ptr != nullptr) {
            auto txs_item = std::make_shared<WaitingTxsItem>();
            txs_item->pool_index = pool_index;
            txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
            txs_item->tx_type = pools::protobuf::kNormalTo;
            if (!TxInPrevConsensus(pool_index, txs_item->txs)) {
                return nullptr;
            }

            return txs_item;
        }

        return nullptr;
    }

    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
            uint32_t pool_index,
            const common::BloomFilter& bloom_filter,
            uint8_t thread_idx) {
        auto txs_item = wtxs[pool_index].FollowerGetTxs(bloom_filter);
        if (txs_item != nullptr) {
            txs_item->pool_index = pool_index;
            if (!TxInPrevConsensus(pool_index, txs_item->txs)) {
                return nullptr;
            }

            return txs_item;
        }

        return nullptr;
    }

    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
            uint32_t pool_index,
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
            uint8_t thread_idx) {
        auto txs_item = wtxs[pool_index].FollowerGetTxs(tx_hash_list);
        if (txs_item != nullptr) {
            txs_item->pool_index = pool_index;
            if (!TxInPrevConsensus(pool_index, txs_item->txs)) {
                return nullptr;
            }

            return txs_item;
        }

        return nullptr;
    }

private:
    bool TxInPrevConsensus(
            uint32_t pool_index,
            const std::map<std::string, pools::TxItemPtr>& txs) {
        for (auto set_iter = pipeline_pools_[pool_index].begin();
                set_iter != pipeline_pools_[pool_index].end(); ++set_iter) {
            for (auto tx_iter = txs.begin(); tx_iter != txs.end(); ++tx_iter) {
                if ((*set_iter)->bloom_filter->Contain(common::Hash::Hash64(tx_iter->first))) {
                    return false;
                }
            }
        }

        return true;
    }

    void LeaderFilterInvalidTx(uint32_t pool_index,
            std::map<std::string, pools::TxItemPtr>& txs) {
        for (auto set_iter = pipeline_pools_[pool_index].begin();
            set_iter != pipeline_pools_[pool_index].end(); ++set_iter) {
            for (auto tx_iter = txs.begin(); tx_iter != txs.end();) {
                if ((*set_iter)->bloom_filter->Contain(common::Hash::Hash64(tx_iter->first))) {
                    txs.erase(tx_iter++);
                    continue;;
                }

                ++tx_iter;
            }
        }
    }

    WaitingTxs wtxs[common::kInvalidPoolIndex];
    std::shared_ptr<pools::TxPoolManager> pool_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::set<std::shared_ptr<WaitingTxsItem>> pipeline_pools_[common::kInvalidPoolIndex];

    DISALLOW_COPY_AND_ASSIGN(WaitingTxsPools);
};


};  // namespace consensus

};  // namespace zjchain