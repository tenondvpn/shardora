#pragma once

#include "consensus/waiting_txs.h"

namespace zjchain {

namespace consensus {

class WaitingTxsPools {
public:
    WaitingTxsPools(std::shared_ptr<pools::TxPoolManager>& pool_mgr) {
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            wtxs[i].Init(i, pool_mgr);
        }
    }

    ~WaitingTxsPools() {}

    std::shared_ptr<WaitingTxsItem> LeaderGetValidTxs(bool direct, uint32_t pool_index) {
        auto txs_item = wtxs[pool_index].LeaderGetValidTxs(direct);
        if (txs_item != nullptr) {
            txs_item->pool_index = pool_index;
            auto& tx_vec = txs_item->txs;
            uint32_t bitcount = ((kBitcountWithItemCount * tx_vec.size()) / 64) * 64;
            if (((kBitcountWithItemCount * tx_vec.size()) % 64) > 0) {
                bitcount += 64;
            }

            txs_item->bloom_filter = std::make_shared<common::BloomFilter>(bitcount, kHashCount);
            std::set<std::string> txs_hash_vec;
            for (auto iter = tx_vec.begin(); iter != tx_vec.end(); ++iter) {
                txs_hash_vec.insert((*iter)->tx_hash);
                txs_item->bloom_filter->Add(common::Hash::Hash64((*iter)->tx_hash));
            }

            // pass by bloomfilter
            auto error_bloomfilter_txs = wtxs[pool_index].FollowerGetTxs(*txs_item->bloom_filter);
            if (error_bloomfilter_txs != nullptr) {
                for (auto iter = error_bloomfilter_txs->txs.begin();
                        iter != error_bloomfilter_txs->txs.end(); ++iter) {
                    auto fiter = txs_hash_vec.find((*iter)->tx_hash);
                    if (fiter == txs_hash_vec.end()) {
                        txs_hash_vec.insert((*iter)->tx_hash);
                        tx_vec.push_back(*iter);
                    }
                }
            }

            auto& all_txs_hash = txs_item->all_txs_hash;
            all_txs_hash.reserve(tx_vec.size() * 32);
            for (auto iter = txs_hash_vec.begin(); iter != txs_hash_vec.end(); ++iter) {
                all_txs_hash.append(*iter);
            }

            all_txs_hash = common::Hash::keccak256(all_txs_hash);
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
            return txs_item;
        }

        return nullptr;
    }

private:
    WaitingTxs wtxs[common::kInvalidPoolIndex];

    DISALLOW_COPY_AND_ASSIGN(WaitingTxsPools);
};


};  // namespace consensus

};  // namespace zjchain