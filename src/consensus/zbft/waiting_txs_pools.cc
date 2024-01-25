#include "consensus/zbft/waiting_txs_pools.h"

#include "consensus/zbft/zbft.h"
#include <_types/_uint32_t.h>

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

void WaitingTxsPools::TxRecover(std::shared_ptr<Zbft>& zbft_ptr) {
    auto& tx_ptr = zbft_ptr->txs_ptr();
    pool_mgr_->TxRecover(tx_ptr->pool_index, tx_ptr->txs);
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::LeaderGetValidTxs(uint32_t pool_index) {
    std::shared_ptr<WaitingTxsItem> txs_item = GetSingleTx(pool_index);
    if (txs_item == nullptr) {
        txs_item = wtxs[pool_index].LeaderGetValidTxs();
    }

    if (txs_item != nullptr) {
        txs_item->pool_index = pool_index;
    }

    for (auto iter = txs_item->txs.begin(); iter != txs_item->txs.end(); iter++) {
        auto msg_ptr = iter->second->msg_ptr;
        ZJC_DEBUG("---4 to: %s", common::Encode::HexEncode(msg_ptr->header.tx_proto().to()).c_str());
    }

    return txs_item;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetSingleTx(uint32_t pool_index) {
    std::shared_ptr<WaitingTxsItem> txs_item = nullptr;
    if (pool_index == common::kRootChainPoolIndex) {
        txs_item = GetTimeblockTx(pool_index, true);
    }

    if (txs_item == nullptr) {
        txs_item = GetStatisticTx(pool_index, true);
    }

    if (txs_item == nullptr) {
        txs_item = GetElectTx(pool_index, "");
    }

    if (txs_item == nullptr) {
        txs_item = GetCrossTx(pool_index, "");
    }

    if (txs_item == nullptr) {
        txs_item = GetToTxs(pool_index, true);
    }

    return txs_item;
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
                tx_ptr->in_consensus = false;
                return nullptr;
            }

            tx_ptr->prev_consensus_tm_us = now_tm;
        }

        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kConsensusRootElectShard;
        ZJC_DEBUG("success to get elect tx: tx hash: %s",
            common::Encode::HexEncode(tx_ptr->tx_hash).c_str());
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
        txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kConsensusRootTimeBlock;
        ZJC_DEBUG("success to get timeblock tx: tx hash: %s",
            common::Encode::HexEncode(tx_ptr->tx_hash).c_str());
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetCrossTx(uint32_t pool_index, bool leader) {
    if (pool_index != common::kRootChainPoolIndex) {
        return nullptr;
    }

    auto tx_ptr = block_mgr_->GetCrossTx(pool_index, leader);
    if (tx_ptr != nullptr) {
        if (leader) {
            auto now_tm = common::TimeUtils::TimestampUs();
            if (tx_ptr->prev_consensus_tm_us + 300000lu > now_tm) {
                tx_ptr->in_consensus = false;
                return nullptr;
            }

            tx_ptr->prev_consensus_tm_us = now_tm;
        }

        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kCross;
        ZJC_DEBUG("1 success get cross tx %u, %d", pool_index, leader);
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetStatisticTx(uint32_t pool_index, bool leader) {
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        if (pool_index != common::kRootChainPoolIndex) {
            return nullptr;
        }
    } else {
        if (pool_index != 0) {
            return nullptr;
        }
    }

//     if (leader) {
//         ZJC_DEBUG("leader now get statistic tx.");
//     }

    auto tx_ptr = block_mgr_->GetStatisticTx(pool_index, leader);
    if (tx_ptr != nullptr) {
        if (leader) {
            auto now_tm = common::TimeUtils::TimestampUs();
            if (tx_ptr->prev_consensus_tm_us + 300000lu > now_tm) {
                tx_ptr->in_consensus = false;
                ZJC_DEBUG("leader failed get statistic tx.");
                return nullptr;
            }

            tx_ptr->prev_consensus_tm_us = now_tm;
        }

        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kStatistic;
        ZJC_DEBUG("1 success get statistic tx %u, %d", pool_index, leader);
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::GetToTxs(uint32_t pool_index, bool leader) {
    if (pool_index == common::kRootChainPoolIndex) {
        return nullptr;
    }

    auto tx_ptr = block_mgr_->GetToTx(pool_index, leader);
    if (tx_ptr != nullptr) {
        if (leader) {
            auto now_tm = common::TimeUtils::TimestampUs();
            if (tx_ptr->prev_consensus_tm_us + 3000000lu > now_tm) {
                tx_ptr->in_consensus = false;
                return nullptr;
            }

            tx_ptr->prev_consensus_tm_us = now_tm;
        }

        auto txs_item = std::make_shared<WaitingTxsItem>();
        txs_item->pool_index = pool_index;
        txs_item->txs[tx_ptr->tx_hash] = tx_ptr;
        txs_item->tx_type = pools::protobuf::kNormalTo;
        ZJC_DEBUG("1 success get to tx %u, %d", pool_index, leader);
        return txs_item;
    }

    return nullptr;
}

std::shared_ptr<WaitingTxsItem> WaitingTxsPools::FollowerGetTxs(
        uint32_t pool_index,
        const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
        uint8_t thread_idx,
        std::vector<uint8_t>* invalid_txs) {
    auto txs_item = wtxs[pool_index].FollowerGetTxs(tx_hash_list, invalid_txs);
    if (txs_item != nullptr) {
        txs_item->pool_index = pool_index;
        return txs_item;
    }

    return nullptr;
}

};  // namespace consensus

};  // namespace zjchain
