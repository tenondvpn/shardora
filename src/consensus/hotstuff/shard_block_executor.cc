#include <consensus/hotstuff/block_executor.h>
#include <consensus/consensus_utils.h>
#include "consensus/hotstuff/hotstuff_utils.h"
#include <shardoravm/shardoravm_utils.h>

namespace shardora {
namespace hotstuff {

Status ShardBlockExecutor::DoTransactionAndCreateTxBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& acc_balance_map,
        shardoravm::ShardorahainHost& shardora_host) {
    // Execute transaction
    auto& block = *view_block->mutable_block_info();
    auto tx_list = block.mutable_tx_list();
    auto& tx_map = txs_ptr->txs;
    tx_list->Reserve(tx_map.size());
    shardora_host.tx_context_.tx_origin = evmc::address{};
    shardora_host.tx_context_.block_coinbase = evmc::address{};
    shardora_host.tx_context_.block_number = block.height();
    shardora_host.tx_context_.block_timestamp = block.timestamp() / 1000;
    uint64_t chain_id = hotstuff::kGlobalChainId;
    shardoravm::Uint64ToEvmcBytes32(
        shardora_host.tx_context_.chain_id,
        chain_id);
    uint32_t tx_index = 0;
    uint64_t block_gas_used = 0;
    block.set_all_gas(block_gas_used);
    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) { 
        auto& tx_info = (*iter)->tx_info;
        auto& block_tx = *tx_list->Add();
        int res = (*iter)->TxToBlockTx(*tx_info, &block_tx);
        if (res != consensus::kConsensusSuccess) {
            tx_list->RemoveLast();
            SHARDORA_WARN("handle tx failed: %u_%u_%lu, tx step: %d, nonce: %lu, res: %d",
                view_block->qc().network_id(), 
                view_block->qc().pool_index(), 
                view_block->qc().view(), 
                (int32_t)block_tx.step(), 
                block_tx.nonce(),
                res);
            continue;
        }

        if (block_tx.step() == pools::protobuf::kContractExcute || block_tx.step() == pools::protobuf::kContractRefund) {
            block_tx.set_from(security_ptr_->GetAddressWithPublicKey(
                (*iter)->tx_info->pubkey()));
        } else {
            block_tx.set_from((*iter)->address_info->addr());
        }

        if (!(*iter)->tx_info->key().empty()) {
            block_tx.set_unique_hash((*iter)->tx_info->key());
        }

        // auto iter = acc_balance_map.find(block_tx.from());
        // if (iter == acc_balance_map.end()) {
        //     if (iter->second.nonce + 1 != block_tx.nonce()) {
        //         tx_list->RemoveLast();
        //         SHARDORA_WARN("handle tx failed: %u_%u_%lu, tx step: %d, nonce: %lu, res: %d",
        //             view_block->qc().network_id(), 
        //             view_block->qc().pool_index(), 
        //             view_block->qc().view(), 
        //             (int32_t)block_tx.step(), 
        //             block_tx.nonce(),
        //             res);
        //         continue;
        //     }
        // }
        block_tx.set_status(consensus::kConsensusSuccess);
        int do_tx_res = (*iter)->HandleTx(
            tx_index++,
            *view_block,
            shardora_host,
            acc_balance_map,
            block_tx);
        if (do_tx_res != consensus::kConsensusSuccess) {
            tx_list->RemoveLast();
            SHARDORA_WARN("handle tx failed: %u_%u_%lu, tx step: %d, nonce: %lu, do_tx_res: %d",
                view_block->qc().network_id(), 
                view_block->qc().pool_index(), 
                view_block->qc().view(), 
                (int32_t)block_tx.step(), 
                block_tx.nonce(),
                do_tx_res);
            continue;
        }

        if (!consensus::CanAddBlockGas(block_gas_used, block_tx.gas_used())) {
            tx_list->RemoveLast();
            SHARDORA_ERROR("block gas used overflow after tx execution: %u_%u_%lu, "
                "tx step: %d, nonce: %lu, block_gas_used: %lu, tx_gas_used: %lu, "
                "block_gas_limit: %lu",
                view_block->qc().network_id(),
                view_block->qc().pool_index(),
                view_block->qc().view(),
                (int32_t)block_tx.step(),
                block_tx.nonce(),
                block_gas_used,
                block_tx.gas_used(),
                consensus::kBlockMaxGasLimit);
            block.set_all_gas(block_gas_used);
            return Status::kError;
        }
        block_gas_used += block_tx.gas_used();
        shardora_host.recorded_logs_.clear();
        // SHARDORA_DEBUG("handle tx success: %u_%u_%lu, tx step: %d, nonce: %lu",
        //     view_block->qc().network_id(), 
        //     view_block->qc().pool_index(), 
        //     view_block->qc().view(), 
        //     block_tx.step(), 
        //     block_tx.nonce());
    }

    block.set_all_gas(block_gas_used);
    
    return Status::kSuccess;    
}

}
}
