#include "consensus/zbft/waiting_txs_pools.h"

namespace shardora {

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

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::LeaderGetValidTxsIdempotently(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index,
        pools::CheckAddrNonceValidFunction addr_nonce_valid_func) {
    auto thread_id = common::GlobalInfo::Instance()->get_thread_index();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    // ZJC_DEBUG("leader get txs coming thread: %d, pool index: %d", thread_id, pool_index);
    #ifdef TEST_NO_CROSS
    std::shared_ptr<WaitingTxsItem> txs_item = nullptr;
    #else
    std::shared_ptr<WaitingTxsItem> txs_item = GetSingleTx(msg_ptr, pool_index, addr_nonce_valid_func);
    #endif

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs_item != nullptr) {
        for (auto iter = txs_item->txs.begin(); iter != txs_item->txs.end(); ++iter) {
            if (addr_nonce_valid_func(*(*iter)->address_info, *(*iter)->tx_info) != 0) {
                txs_item = nullptr;
                break;
            }
        }
    }

    if (txs_item == nullptr) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        txs_item = wtxs[pool_index].LeaderGetValidTxsIdempotently(msg_ptr, addr_nonce_valid_func);
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs_item != nullptr) {
        txs_item->pool_index = pool_index;
        if (!txs_item->txs.empty()) {
            auto first_tx = *(txs_item->txs.begin());
            ZJC_DEBUG("success leader get single txs coming thread: %d, "
                "pool index: %d, tx count: %d, nonce: %lu, step: %d", 
                thread_id, pool_index, txs_item->txs.size(), 
                first_tx->tx_info->nonce(), 
                first_tx->tx_info->step());
        }
    } else {
        ZJC_DEBUG("failed leader get txs coming thread: %d, pool index: %d, tx count: %d", 
            thread_id, pool_index, 0);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return txs_item;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetSingleTx(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index,
        pools::CheckAddrNonceValidFunction addr_nonce_valid_func) {
    ZJC_DEBUG("get single tx pool: %u", pool_index);
    std::shared_ptr<WaitingTxsItem> txs_item = nullptr;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (pool_index == common::kImmutablePoolSize) {
        ZJC_DEBUG("leader get time tx tmblock_tx_ptr: %u", pool_index);
        txs_item = GetTimeblockTx(pool_index, true);
        if (txs_item) {
            auto iter = txs_item->txs.begin();
            if (iter == txs_item->txs.end() || addr_nonce_valid_func(
                    *(*iter)->address_info, 
                    *(*iter)->tx_info) != 0) {
                txs_item = nullptr;
            }
        }

        ZJC_DEBUG("GetTimeblockTx: %d", (txs_item != nullptr));
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs_item == nullptr && pool_index == common::kImmutablePoolSize) {
        txs_item = GetToTxs(pool_index, "");
        ZJC_DEBUG("GetToTxs: %d", (txs_item != nullptr));
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs_item == nullptr) {
        // if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        //     if (pool_index == common::kImmutablePoolSize) {
        //         ZJC_DEBUG("now get statistic tx leader now GetStatisticTx pool_index: %d", pool_index);
        //     }
        // }
        
        txs_item = GetStatisticTx(pool_index, "");
        ZJC_DEBUG("GetStatisticTx: %d", (txs_item != nullptr));
        if (txs_item) {
            auto iter = txs_item->txs.begin();
            if (iter == txs_item->txs.end() || !addr_nonce_valid_func(
                    *(*iter)->address_info, 
                    *(*iter)->tx_info)) {
                txs_item = nullptr;
            }
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs_item == nullptr) {
        txs_item = GetElectTx(pool_index, "");
        if (txs_item) {
            auto iter = txs_item->txs.begin();
            if (iter == txs_item->txs.end() || !addr_nonce_valid_func(
                    *(*iter)->address_info, 
                    *(*iter)->tx_info)) {
                txs_item = nullptr;
            }
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return txs_item;
}

bool WaitingTxsPools::HasSingleTx(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index, 
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    if (timeblock_mgr_->HasTimeblockTx(pool_index, tx_valid_func)) {
        return true;
    }

    if (block_mgr_->HasSingleTx(msg_ptr, pool_index, tx_valid_func)) {
        return true;
    }

    return false;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetElectTx(
        uint32_t pool_index,
        const std::string& tx_hash) {
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        return nullptr;
    }

    if (pool_index == common::kImmutablePoolSize) {
        return nullptr;
    }

    auto tx_ptr = block_mgr_->GetElectTx(pool_index, tx_hash);
    if (tx_ptr != nullptr) {
        if (tx_hash.empty()) {
            auto now_tm = common::TimeUtils::TimestampUs();
            if (tx_ptr->prev_consensus_tm_us + 300000lu > now_tm) {
                return nullptr;
            }

            tx_ptr->prev_consensus_tm_us = now_tm;
        }

        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs.push_back(tx_ptr);
        txs_item->tx_type = pools::protobuf::kConsensusRootElectShard;
        ZJC_DEBUG("single tx success to get elect tx: tx key: %s, nonce: %lu",
            common::Encode::HexEncode(tx_ptr->tx_key).c_str(),
            tx_ptr->tx_info->nonce());
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetTimeblockTx(uint32_t pool_index, bool leader) {
    if (pool_index != common::kImmutablePoolSize ||
            common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        return nullptr;
    }

    auto tx_ptr = timeblock_mgr_->tmblock_tx_ptr(leader, pool_index);
    if (tx_ptr != nullptr) {
        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        if (tx_ptr->tx_key.empty()) {
            assert(false);
            return nullptr;
        }
        
        txs_item->txs.push_back(tx_ptr);
        txs_item->tx_type = pools::protobuf::kConsensusRootTimeBlock;
        ZJC_DEBUG("single tx success to get timeblock tx: tx key: %s, nonce: %lu",
            common::Encode::HexEncode(tx_ptr->tx_key).c_str(), 
            tx_ptr->tx_info->nonce());
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetStatisticTx(
        uint32_t pool_index, 
        const std::string& tx_gid) {
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        if (pool_index != common::kImmutablePoolSize) {
            return nullptr;
        }
    } else {
        if (pool_index != 0) {
            return nullptr;
        }
    }

    bool leader = tx_gid.empty();
    auto tx_ptr = block_mgr_->GetStatisticTx(pool_index, 0);
    if (tx_ptr != nullptr) {
        if (leader) {
            auto now_tm = common::TimeUtils::TimestampUs();
            if (tx_ptr->prev_consensus_tm_us + 300000lu > now_tm) {
                ZJC_DEBUG("leader failed get statistic tx.");
                return nullptr;
            }

            tx_ptr->prev_consensus_tm_us = now_tm;
        }

        if (tx_ptr->tx_key.empty()) {
            assert(false);
            return nullptr;
        }
        
        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs.push_back(tx_ptr);
        txs_item->tx_type = pools::protobuf::kStatistic;
        ZJC_DEBUG("single tx success get statistic tx %u, %d, tx key: %s, nonce: %lu", 
            pool_index, leader, 
            common::Encode::HexEncode(tx_ptr->tx_key).c_str(),
            tx_ptr->tx_info->nonce());
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetToTxs(
        uint32_t pool_index, 
        const std::string& tx_hash) {
    if (network::IsSameToLocalShard(network::kRootCongressNetworkId) &&
            pool_index != common::kImmutablePoolSize) {
        return nullptr;
    }

    bool leader = tx_hash.empty();
    pools::TxItemPtr tx_ptr = block_mgr_->GetToTx(pool_index, tx_hash);
    if (tx_ptr != nullptr) {
        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs.push_back(tx_ptr);
        txs_item->tx_type = pools::protobuf::kNormalTo;
        ZJC_DEBUG("single tx success get to tx %u, is leader: %d, tx key: %s, nonce: %lu", 
            pool_index, leader, 
            common::Encode::HexEncode(tx_ptr->tx_key).c_str(),
            tx_ptr->tx_info->nonce());
        return txs_item;
    } else {
        if (leader) {
            ZJC_DEBUG("leader get to tx coming failed 0");
        }
    }

    return nullptr;
}

};  // namespace consensus

};  // namespace shardora
