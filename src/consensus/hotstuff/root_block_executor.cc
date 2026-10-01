#include <consensus/hotstuff/block_executor.h>
#include <consensus/consensus_utils.h>
#include "consensus/hotstuff/hotstuff_utils.h"
#include <shardoravm/shardoravm_utils.h>

namespace shardora {
namespace hotstuff {

namespace {

bool AddBlockTxGas(
        const view_block::protobuf::ViewBlockItem* view_block,
        const block::protobuf::BlockTx& tx,
        uint64_t* block_gas_used) {
    if (consensus::CanAddBlockGas(*block_gas_used, tx.gas_used())) {
        *block_gas_used += tx.gas_used();
        return true;
    }

    SHARDORA_ERROR("block gas used overflow after tx execution: %u_%u_%lu, "
        "tx step: %d, nonce: %lu, block_gas_used: %lu, tx_gas_used: %lu, "
        "block_gas_limit: %lu",
        view_block->qc().network_id(),
        view_block->qc().pool_index(),
        view_block->qc().view(),
        (int32_t)tx.step(),
        tx.nonce(),
        *block_gas_used,
        tx.gas_used(),
        consensus::kBlockMaxGasLimit);
    return false;
}

void SetBlockGasUsed(
        view_block::protobuf::ViewBlockItem* view_block,
        uint64_t block_gas_used) {
    view_block->mutable_block_info()->set_all_gas(block_gas_used);
}

} // namespace

Status RootBlockExecutor::DoTransactionAndCreateTxBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& balance_map,
        shardoravm::ShardorahainHost& shardora_host) {
    SetBlockGasUsed(view_block, 0);
    if (txs_ptr->txs.size() == 1) {
        auto& tx = *(txs_ptr->txs.begin());
        switch (tx->tx_info->step()) {
        case pools::protobuf::kConsensusRootElectShard:
            return RootCreateElectConsensusShardBlock(txs_ptr, view_block, balance_map, shardora_host);
        case pools::protobuf::kConsensusRootTimeBlock:
        case pools::protobuf::kStatistic:
        case pools::protobuf::kCross:
            return RootDefaultTx(txs_ptr, view_block, balance_map, shardora_host);
        default:
            return RootCreateAccountAddressBlock(txs_ptr, view_block, balance_map, shardora_host);
        }
    }

    return RootCreateAccountAddressBlock(txs_ptr, view_block, balance_map, shardora_host);
}

Status RootBlockExecutor::RootDefaultTx(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& balance_map,
        shardoravm::ShardorahainHost& shardora_host) {
    auto* block = view_block->mutable_block_info();
    auto tx_list = block->mutable_tx_list();
    auto& tx = *tx_list->Add();
    auto iter = txs_ptr->txs.begin();
    uint64_t block_gas_used = 0;
    (*iter)->TxToBlockTx(*(*iter)->tx_info, &tx);
    int do_tx_res = (*iter)->HandleTx(
        0,
        *view_block,
        shardora_host,
        balance_map,
        tx);

    if (do_tx_res != consensus::kConsensusSuccess) {
        tx_list->RemoveLast();
        // //assert(false);
        SetBlockGasUsed(view_block, block_gas_used);
        return Status::kSuccess;
    }

    if (!AddBlockTxGas(view_block, tx, &block_gas_used)) {
        tx_list->RemoveLast();
        SetBlockGasUsed(view_block, block_gas_used);
        return Status::kError;
    }

    SetBlockGasUsed(view_block, block_gas_used);
    return Status::kSuccess;
}

Status RootBlockExecutor::RootCreateAccountAddressBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& acc_balance_map,
        shardoravm::ShardorahainHost& shardora_host) {
    auto* block = view_block->mutable_block_info();
    auto tx_list = block->mutable_tx_list();
    auto& tx_map = txs_ptr->txs;
    uint32_t tx_index = 0;
    uint64_t block_gas_used = 0;
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) {
        auto& tx = *tx_list->Add();
        auto& src_tx = (*iter)->tx_info;
        (*iter)->TxToBlockTx(*src_tx, &tx);
        int do_tx_res = (*iter)->HandleTx(
            tx_index++,
            *view_block,
            shardora_host,
            acc_balance_map,
            tx);

        if (do_tx_res != consensus::kConsensusSuccess) {
            tx_list->RemoveLast();
            // //assert(false);
            continue;
        }

        if (!AddBlockTxGas(view_block, tx, &block_gas_used)) {
            tx_list->RemoveLast();
            SetBlockGasUsed(view_block, block_gas_used);
            return Status::kError;
        }
    }

    SetBlockGasUsed(view_block, block_gas_used);
    return Status::kSuccess;
}

Status RootBlockExecutor::RootCreateElectConsensusShardBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& acc_balance_map,
        shardoravm::ShardorahainHost& shardora_host) {
    auto& tx_map = txs_ptr->txs;
    if (tx_map.size() != 1) {
        SetBlockGasUsed(view_block, 0);
        return Status::kSuccess;
    }

    auto iter = tx_map.begin();
    if ((*iter)->tx_info->step() != pools::protobuf::kConsensusRootElectShard) {
        //assert(false);
        SetBlockGasUsed(view_block, 0);
        return Status::kSuccess;
    }

    auto* block = view_block->mutable_block_info();
    auto tx_list = block->mutable_tx_list();
    auto& tx = *tx_list->Add();
    uint64_t block_gas_used = 0;
    (*iter)->TxToBlockTx(*(*iter)->tx_info, &tx);
    int do_tx_res = (*iter)->HandleTx(
        0,
        *view_block,
        shardora_host,
        acc_balance_map,
        tx);
    if (do_tx_res != consensus::kConsensusSuccess) {
        tx_list->RemoveLast();
        ////assert(false);
        SHARDORA_WARN("consensus elect tx failed!");
        SetBlockGasUsed(view_block, block_gas_used);
        return Status::kSuccess;
    }

    if (!AddBlockTxGas(view_block, tx, &block_gas_used)) {
        tx_list->RemoveLast();
        SetBlockGasUsed(view_block, block_gas_used);
        return Status::kError;
    }

    SetBlockGasUsed(view_block, block_gas_used);
    return Status::kSuccess;
}

}
}

