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
        pools::CheckGidValidFunction gid_vlid_func) {
    auto thread_id = common::GlobalInfo::Instance()->get_thread_index();
    ADD_DEBUG_PROCESS_TIMESTAMP();
    // ZJC_DEBUG("leader get txs coming thread: %d, pool index: %d", thread_id, pool_index);
    #ifdef TEST_NO_CROSS
    std::shared_ptr<WaitingTxsItem> txs_item = nullptr;
    #else
    std::shared_ptr<WaitingTxsItem> txs_item = GetSingleTx(pool_index, gid_vlid_func);
    #endif

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs_item != nullptr) {
        for (auto iter = txs_item->txs.begin(); iter != txs_item->txs.end(); ++iter) {
            if (!gid_vlid_func(iter->second->tx_info.gid())) {
                txs_item = nullptr;
                break;
            }
        }
    }

    if (txs_item == nullptr) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        txs_item = wtxs[pool_index].LeaderGetValidTxsIdempotently(msg_ptr, gid_vlid_func);
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (txs_item != nullptr) {
        txs_item->pool_index = pool_index;
        if (!txs_item->txs.empty()) {
            auto first_tx = txs_item->txs.begin()->second;
            ZJC_DEBUG("success leader get single txs coming thread: %d, "
                "pool index: %d, tx count: %d, gid: %s, step: %d", 
                thread_id, pool_index, txs_item->txs.size(), 
                common::Encode::HexEncode(first_tx->tx_info.gid()).c_str(), 
                first_tx->tx_info.step());
        }
    } else {
        ZJC_DEBUG("failed leader get txs coming thread: %d, pool index: %d, tx count: %d", 
            thread_id, pool_index, 0);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return txs_item;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetSingleTx(
        uint32_t pool_index,
        pools::CheckGidValidFunction gid_vlid_func) {
    ZJC_DEBUG("get single tx pool: %u", pool_index);
    std::shared_ptr<WaitingTxsItem> txs_item = nullptr;
    if (pool_index == common::kRootChainPoolIndex) {
        ZJC_DEBUG("leader get time tx tmblock_tx_ptr: %u", pool_index);
        txs_item = GetTimeblockTx(pool_index, true);
        ZJC_DEBUG("GetTimeblockTx: %d", (txs_item != nullptr));
    }

    if (txs_item == nullptr && pool_index == common::kImmutablePoolSize) {
        auto gid = GetToTxGid();
        if (gid_vlid_func(gid)) {
            txs_item = GetToTxs(pool_index, "");
            ZJC_DEBUG("GetToTxs: %d", (txs_item != nullptr));
        } else {
            ZJC_DEBUG("GetToTxGid failed: %d, gid: %s", 
                (txs_item != nullptr), 
                common::Encode::HexEncode(gid).c_str());
        }
    }

    if (txs_item == nullptr) {
        // if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        //     if (pool_index == common::kRootChainPoolIndex) {
        //         ZJC_DEBUG("now get statistic tx leader now GetStatisticTx pool_index: %d", pool_index);
        //     }
        // }
        
        txs_item = GetStatisticTx(pool_index, "");
        ZJC_DEBUG("GetStatisticTx: %d", (txs_item != nullptr));
    }

    if (txs_item == nullptr) {
        txs_item = GetElectTx(pool_index, "");
    }

    return txs_item;
}

bool WaitingTxsPools::HasSingleTx(
        uint32_t pool_index, 
        pools::CheckGidValidFunction gid_valid_fn) {
    if (timeblock_mgr_->HasTimeblockTx(pool_index, gid_valid_fn)) {
        return true;
    }

    if (block_mgr_->HasSingleTx(pool_index, gid_valid_fn)) {
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

    if (pool_index == common::kRootChainPoolIndex) {
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
        txs_item->txs[tx_ptr->unique_tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kConsensusRootElectShard;
        ZJC_DEBUG("single tx success to get elect tx: tx hash: %s, gid: %s",
            common::Encode::HexEncode(tx_ptr->unique_tx_hash).c_str(),
            common::Encode::HexEncode(tx_ptr->tx_info.gid()).c_str());
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetTimeblockTx(uint32_t pool_index, bool leader) {
    if (pool_index != common::kRootChainPoolIndex ||
            common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        return nullptr;
    }

    auto tx_ptr = timeblock_mgr_->tmblock_tx_ptr(leader, pool_index);
    if (tx_ptr != nullptr) {
        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        if (tx_ptr->unique_tx_hash.empty()) {
            tx_ptr->unique_tx_hash = pools::GetTxMessageHash(tx_ptr->tx_info);
        }
        
        txs_item->txs[tx_ptr->unique_tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kConsensusRootTimeBlock;
        ZJC_DEBUG("single tx success to get timeblock tx: tx hash: %s, gid: %s",
            common::Encode::HexEncode(tx_ptr->unique_tx_hash).c_str(), 
            common::Encode::HexEncode(tx_ptr->tx_info.gid()).c_str());
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetStatisticTx(
        uint32_t pool_index, 
        const std::string& tx_gid) {
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        if (pool_index != common::kRootChainPoolIndex) {
            return nullptr;
        }
    } else {
        if (pool_index != 0) {
            return nullptr;
        }
    }

    bool leader = tx_gid.empty();
    auto tx_ptr = block_mgr_->GetStatisticTx(pool_index, tx_gid);
    if (tx_ptr != nullptr) {
        if (leader) {
            auto now_tm = common::TimeUtils::TimestampUs();
            if (tx_ptr->prev_consensus_tm_us + 300000lu > now_tm) {
                ZJC_DEBUG("leader failed get statistic tx.");
                return nullptr;
            }

            tx_ptr->prev_consensus_tm_us = now_tm;
        }

        if (tx_ptr->unique_tx_hash.empty()) {
            tx_ptr->unique_tx_hash = pools::GetTxMessageHash(tx_ptr->tx_info);
        }
        
        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs[tx_ptr->unique_tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kStatistic;
        ZJC_DEBUG("single tx success get statistic tx %u, %d, txhash: %s, gid: %s", 
            pool_index, leader, 
            common::Encode::HexEncode(tx_ptr->unique_tx_hash).c_str(),
            common::Encode::HexEncode(tx_ptr->tx_info.gid()).c_str());
        return txs_item;
    }

    return nullptr;
}

std::string WaitingTxsPools::GetToTxGid() {
    return block_mgr_->GetToTxGid();
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetToTxs(
        uint32_t pool_index, 
        const std::string& tx_hash) {
    if (pool_index != common::kImmutablePoolSize) {
        return nullptr;
    }

    bool leader = tx_hash.empty();
    pools::TxItemPtr tx_ptr = block_mgr_->GetToTx(pool_index, tx_hash);
    if (tx_ptr != nullptr) {
        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs[tx_ptr->unique_tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kNormalTo;
        ZJC_DEBUG("single tx success get to tx %u, is leader: %d, txhash: %s, gid: %s", 
            pool_index, leader, 
            common::Encode::HexEncode(tx_ptr->unique_tx_hash).c_str(),
            common::Encode::HexEncode(tx_ptr->tx_info.gid()).c_str());
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
