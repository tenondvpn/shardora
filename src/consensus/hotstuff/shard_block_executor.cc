#include <consensus/hotstuff/block_executor.h>
#include <zjcvm/zjcvm_utils.h>

namespace shardora {
namespace hotstuff {

Status ShardBlockExecutor::DoTransactionAndCreateTxBlock(
        const std::shared_ptr<consensus::WaitingTxsItem> &txs_ptr,
        std::shared_ptr<block::protobuf::Block>& block) {
    // 执行交易
    auto tx_list = block->mutable_tx_list();
    auto& tx_map = txs_ptr->txs;
    tx_list->Reserve(tx_map.size());
    std::unordered_map<std::string, int64_t> acc_balance_map;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = block->height();
    zjc_host.tx_context_.block_timestamp = block->timestamp() / 1000;
    uint64_t chain_id = (((uint64_t)block->network_id()) << 32 | (uint64_t)block->pool_index());
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chain_id);

    for (auto iter = tx_map.begin(); iter != tx_map.end(); ++iter) { 
        auto& tx_info = iter->second->tx_info;
        auto& block_tx = *tx_list->Add();
        int res = iter->second->TxToBlockTx(tx_info, db_batch_, &block_tx);
        if (res != consensus::kConsensusSuccess) {
            tx_list->RemoveLast();
            continue;
        }

        if (block_tx.step() == pools::protobuf::kContractExcute) {
            block_tx.set_from(security_ptr_->GetAddress(
                iter->second->tx_info.pubkey()));
        } else {
            block_tx.set_from(iter->second->address_info->addr());
        }

        block_tx.set_status(consensus::kConsensusSuccess);
        int do_tx_res = iter->second->HandleTx(
            *block,
            db_batch_,
            zjc_host,
            acc_balance_map,
            block_tx);
        if (do_tx_res != consensus::kConsensusSuccess) {
            tx_list->RemoveLast();
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
    }
    
    return Status::kSuccess;    
}

}
}
