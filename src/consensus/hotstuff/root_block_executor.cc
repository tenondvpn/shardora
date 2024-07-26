#include <consensus/hotstuff/block_executor.h>
#include <zjcvm/zjcvm_utils.h>

namespace shardora {
namespace hotstuff {

Status RootBlockExecutor::DoTransactionAndCreateTxBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        std::shared_ptr<block::protobuf::Block>& block) {
    if (txs_ptr->txs.size() == 1) {
        auto& tx = *txs_ptr->txs.begin()->second;
        switch (tx.tx_info.step()) {
        case pools::protobuf::kConsensusRootElectShard:
            RootCreateElectConsensusShardBlock(txs_ptr, block);
            break;
        case pools::protobuf::kConsensusRootTimeBlock:
        case pools::protobuf::kStatistic:
        case pools::protobuf::kCross:
            RootDefaultTx(txs_ptr, block);
            break;
        default:
            RootCreateAccountAddressBlock(txs_ptr, block);
            break;
        }
    } else {
        RootCreateAccountAddressBlock(txs_ptr, block);
    }
    
    return Status::kSuccess;
}

void RootBlockExecutor::RootDefaultTx(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        std::shared_ptr<block::protobuf::Block>& block) {
    auto tx_list = block->mutable_tx_list();
    auto& tx = *tx_list->Add();
    auto iter = txs_ptr->txs.begin();
    iter->second->TxToBlockTx(iter->second->tx_info, db_batch_, &tx);
}

void RootBlockExecutor::RootCreateAccountAddressBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        std::shared_ptr<block::protobuf::Block>& block) {
    auto tx_list = block->mutable_tx_list();
    auto& tx_map = txs_ptr->txs;
    std::unordered_map<std::string, int64_t> acc_balance_map;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
        auto& tx = *tx_list->Add();
        auto& src_tx = iter->second->tx_info;
        iter->second->TxToBlockTx(src_tx, db_batch_, &tx);
        // create address must to and have transfer amount
        if (tx.step() == pools::protobuf::kRootCreateAddress) {
            if (!tx.has_contract_code() && tx.amount() <= 0) {
                ZJC_DEBUG("tx invalid step: %d, amount: %lu, src: %d, %lu",
                    tx.step(), tx.amount(), src_tx.step(), src_tx.amount());
                tx_list->RemoveLast();
                assert(false);
                continue;    
            }
        }

        int do_tx_res = iter->second->HandleTx(
            *block,
            db_batch_,
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
        std::shared_ptr<block::protobuf::Block>& block) {
    auto& tx_map = txs_ptr->txs;
    if (tx_map.size() != 1) {
        return;
    }

    auto iter = tx_map.begin();
    if (iter->second->tx_info.step() != pools::protobuf::kConsensusRootElectShard) {
        assert(false);
        return;
    }

    auto tx_list = block->mutable_tx_list();
    auto& tx = *tx_list->Add();
    iter->second->TxToBlockTx(iter->second->tx_info, db_batch_, &tx);
    std::unordered_map<std::string, int64_t> acc_balance_map;
    int do_tx_res = iter->second->HandleTx(
        *block,
        db_batch_,
        zjc_host,
        acc_balance_map,
        tx);
    if (do_tx_res != consensus::kConsensusSuccess) {
        tx_list->RemoveLast();
        //assert(false);
        ZJC_WARN("consensus elect tx failed!");
        return;
    }

    ZJC_DEBUG("elect use elect height: %lu, now elect height: %lu", block->electblock_height(), block->height());
}

}
}

