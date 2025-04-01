#include <consensus/hotstuff/block_executor.h>
#include "consensus/hotstuff/hotstuff_utils.h"
#include <zjcvm/zjcvm_utils.h>

namespace shardora {
namespace hotstuff {

Status RootBlockExecutor::DoTransactionAndCreateTxBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& balance_map,
        zjcvm::ZjchainHost& zjc_host) {
    if (txs_ptr->txs.size() == 1) {
        auto& tx = *txs_ptr->txs.begin()->second;
        switch (tx.tx_info->step()) {
        case pools::protobuf::kConsensusRootElectShard:
            RootCreateElectConsensusShardBlock(txs_ptr, view_block, balance_map, zjc_host);
            break;
        case pools::protobuf::kConsensusRootTimeBlock:
        case pools::protobuf::kStatistic:
        case pools::protobuf::kCross:
            RootDefaultTx(txs_ptr, view_block, balance_map);
            break;
        default:
            RootCreateAccountAddressBlock(txs_ptr, view_block, balance_map, zjc_host);
            break;
        }
    } else {
        RootCreateAccountAddressBlock(txs_ptr, view_block, balance_map, zjc_host);
    }
    
    return Status::kSuccess;
}

void RootBlockExecutor::RootDefaultTx(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& balance_map) {
    auto* block = view_block->mutable_block_info();
    auto tx_list = block->mutable_tx_list();
    auto& tx = *tx_list->Add();
    auto iter = txs_ptr->txs.begin();
    balance_map[tx.to()] = 0;
    iter->second->TxToBlockTx(*iter->second->tx_info, &tx);
}

void RootBlockExecutor::RootCreateAccountAddressBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& acc_balance_map,
        zjcvm::ZjchainHost& zjc_host) {
    auto* block = view_block->mutable_block_info();
    auto tx_list = block->mutable_tx_list();
    auto& tx_map = txs_ptr->txs;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
        auto& tx = *tx_list->Add();
        auto& src_tx = iter->second->tx_info;
        iter->second->TxToBlockTx(*src_tx, &tx);
        // create address must to and have transfer amount
        if (tx.step() == pools::protobuf::kRootCreateAddress) {
            if (!tx.has_contract_code() && tx.amount() <= 0) {
                ZJC_DEBUG("tx invalid step: %d, amount: %lu, src: %d, %lu",
                    tx.step(), tx.amount(), src_tx->step(), src_tx->amount());
                tx_list->RemoveLast();
                assert(false);
                continue;    
            }
        }

        int do_tx_res = iter->second->HandleTx(
            *view_block,
            zjc_host,
            acc_balance_map,
            tx);

        if (do_tx_res != consensus::kConsensusSuccess) {
            tx_list->RemoveLast();
            assert(false);
            continue;
        }
    }
}

void RootBlockExecutor::RootCreateElectConsensusShardBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& acc_balance_map,
        zjcvm::ZjchainHost& zjc_host) {
    auto& tx_map = txs_ptr->txs;
    if (tx_map.size() != 1) {
        return;
    }

    auto iter = tx_map.begin();
    if (iter->second->tx_info->step() != pools::protobuf::kConsensusRootElectShard) {
        assert(false);
        return;
    }

    auto* block = view_block->mutable_block_info();
    auto tx_list = block->mutable_tx_list();
    auto& tx = *tx_list->Add();
    iter->second->TxToBlockTx(*iter->second->tx_info, &tx);
    int do_tx_res = iter->second->HandleTx(
        *view_block,
        zjc_host,
        acc_balance_map,
        tx);
    if (do_tx_res != consensus::kConsensusSuccess) {
        tx_list->RemoveLast();
        //assert(false);
        ZJC_WARN("consensus elect tx failed!");
        return;
    }
}

}
}

