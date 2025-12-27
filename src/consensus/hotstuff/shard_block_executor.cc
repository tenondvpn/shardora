#include <consensus/hotstuff/block_executor.h>
#include "consensus/hotstuff/hotstuff_utils.h"
#include <zjcvm/zjcvm_utils.h>

namespace shardora {
namespace hotstuff {

Status ShardBlockExecutor::DoTransactionAndCreateTxBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& acc_balance_map,
        zjcvm::ZjchainHost& zjc_host) {
    // Execute transaction
    auto& block = *view_block->mutable_block_info();
    auto tx_list = block.mutable_tx_list();
    auto& tx_map = txs_ptr->txs;
    tx_list->Reserve(tx_map.size());
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = block.height();
    zjc_host.tx_context_.block_timestamp = block.timestamp() / 1000;
    uint64_t chain_id = (((uint64_t)view_block->qc().network_id()) << 32 | (uint64_t)view_block->qc().pool_index());
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chain_id);

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

        if (block_tx.step() == pools::protobuf::kContractExcute) {
            block_tx.set_from(security_ptr_->GetAddress(
                (*iter)->tx_info->pubkey()));
        } else {
            block_tx.set_from((*iter)->address_info->addr());
        }

        block_tx.set_status(consensus::kConsensusSuccess);
        int do_tx_res = (*iter)->HandleTx(
            *view_block,
            zjc_host,
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

        for (auto event_iter = zjc_host.recorded_logs_.begin();
                event_iter != zjc_host.recorded_logs_.end(); ++event_iter) {
            auto log = block_tx.add_events();
            log->set_data((*event_iter).data);
            for (auto topic_iter = (*event_iter).topics.begin();
                    topic_iter != (*event_iter).topics.end(); ++topic_iter) {
                log->add_topics(std::string((char*)(*topic_iter).bytes, sizeof((*topic_iter).bytes)));
            }
        }

        zjc_host.recorded_logs_.clear();
        // SHARDORA_DEBUG("handle tx success: %u_%u_%lu, tx step: %d, nonce: %lu",
        //     view_block->qc().network_id(), 
        //     view_block->qc().pool_index(), 
        //     view_block->qc().view(), 
        //     block_tx.step(), 
        //     block_tx.nonce());
    }
    
    return Status::kSuccess;    
}

}
}
