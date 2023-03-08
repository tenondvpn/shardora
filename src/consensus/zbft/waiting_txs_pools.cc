#include "consensus/zbft/waiting_txs_pools.h"

#include "consensus/zbft/zbft.h"

namespace zjchain {

namespace consensus {

WaitingTxsPools::WaitingTxsPools(
        std::shared_ptr<pools::TxPoolManager>& pool_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& timeblock_mgr)
        : pool_mgr_(pool_mgr), block_mgr_(block_mgr), timeblock_mgr_(timeblock_mgr) {
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        wtxs[i].Init(i, pool_mgr);
    }
}

WaitingTxsPools::~WaitingTxsPools() {}

void WaitingTxsPools::TxOver(std::shared_ptr<Zbft>& zbft_ptr) {
    auto& tx_ptr = zbft_ptr->txs_ptr();
    pool_mgr_->TxOver(tx_ptr->pool_index, tx_ptr->txs);
    auto& item_set = pipeline_pools_[tx_ptr->pool_index];
    for (auto set_iter = item_set.begin(); set_iter != item_set.end(); ++set_iter) {
        if (*set_iter == zbft_ptr) {
            item_set.erase(set_iter);
            break;
        }
    }
}

void WaitingTxsPools::TxRecover(std::shared_ptr<Zbft>& zbft_ptr) {
    auto& tx_ptr = zbft_ptr->txs_ptr();
    pool_mgr_->TxRecover(tx_ptr->pool_index, tx_ptr->txs);
    auto& item_set = pipeline_pools_[tx_ptr->pool_index];
    for (auto set_iter = item_set.begin(); set_iter != item_set.end(); ++set_iter) {
        if (*set_iter == zbft_ptr) {
            item_set.erase(set_iter);
            break;
        }
    }
}

void WaitingTxsPools::LockPool(std::shared_ptr<Zbft>& zbft_ptr) {
    pipeline_pools_[zbft_ptr->txs_ptr()->pool_index].push_back(zbft_ptr);
}

uint64_t WaitingTxsPools::latest_height(uint32_t pool_index) const {
    if (pipeline_pools_[pool_index].empty()) {
        return pool_mgr_->latest_height(pool_index);
    }

    return pipeline_pools_[pool_index].back()->prpare_block()->height();
}

std::string WaitingTxsPools::latest_hash(uint32_t pool_index) const {
    if (pipeline_pools_[pool_index].empty()) {
        return pool_mgr_->latest_hash(pool_index);
    }

    return pipeline_pools_[pool_index].back()->prpare_block()->hash();
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::LeaderGetValidTxs(
        bool direct,
        uint32_t pool_index) {
    std::shared_ptr<WaitingTxsItem> txs_item = GetSingleTx(pool_index);
    if (txs_item == nullptr) {
        txs_item = wtxs[pool_index].LeaderGetValidTxs(direct);
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

            FilterInvalidTx(pool_index, txs_item->txs);
            if (txs_item->txs.empty()) {
                txs_item = nullptr;
            } else {
                auto& all_txs_hash = txs_item->all_txs_hash;
                all_txs_hash.reserve(tx_map.size() * 32);
                for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
                    all_txs_hash.append(iter->first);
                }

                all_txs_hash = common::Hash::keccak256(all_txs_hash);
            }
        }
    }

    return txs_item;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetSingleTx(uint32_t pool_index) {
    std::shared_ptr<WaitingTxsItem> txs_item = GetTimeblockTx(pool_index);
    if (txs_item == nullptr) {
        txs_item = GetToTxs(pool_index);
    }

    return txs_item;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetTimeblockTx(uint32_t pool_index) {
    if (pool_index != common::kRootChainPoolIndex ||
            common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        return nullptr;
    }

    auto tx_ptr = timeblock_mgr_->tmblock_tx_ptr();
    if (tx_ptr != nullptr) {
        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kConsensusRootTimeBlock;
        FilterInvalidTx(pool_index, txs_item->txs);
        if (txs_item->txs.empty()) {
            return nullptr;
        }

        ZJC_DEBUG("success get timeblock tx.");
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetToTxs(uint32_t pool_index) {
    auto tx_ptr = block_mgr_->GetToTx(pool_index);
    if (tx_ptr != nullptr) {
        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kNormalTo;
        FilterInvalidTx(pool_index, txs_item->txs);
        if (txs_item->txs.empty()) {
            return nullptr;
        }

        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::FollowerGetTxs(
        uint32_t pool_index,
        const common::BloomFilter& bloom_filter,
        uint8_t thread_idx) {
    auto txs_item = wtxs[pool_index].FollowerGetTxs(bloom_filter);
    if (txs_item != nullptr) {
        txs_item->pool_index = pool_index;
        FilterInvalidTx(pool_index, txs_item->txs);
        if (txs_item->txs.empty()) {
            return nullptr;
        }

        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::FollowerGetTxs(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
        uint8_t thread_idx) {
    auto txs_item = wtxs[pool_index].FollowerGetTxs(tx_hash_list);
    if (txs_item != nullptr) {
        txs_item->pool_index = pool_index;
        FilterInvalidTx(pool_index, txs_item->txs);
        if (txs_item->txs.empty()) {
            return nullptr;
        }

        return txs_item;
    }

    return nullptr;
}

void WaitingTxsPools::FilterInvalidTx(uint32_t pool_index,
        std::map<std::string, pools::TxItemPtr>& txs) {
    for (auto set_iter = pipeline_pools_[pool_index].begin();
        set_iter != pipeline_pools_[pool_index].end(); ++set_iter) {
        for (auto tx_iter = txs.begin(); tx_iter != txs.end();) {
            auto exist_tx_iter = (*set_iter)->txs_ptr()->txs.find(tx_iter->first);
            if (exist_tx_iter != (*set_iter)->txs_ptr()->txs.end()) {
                txs.erase(tx_iter++);
                continue;
            }

            ++tx_iter;
        }
    }
}

};  // namespace consensus

};  // namespace zjchain