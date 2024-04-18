#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/types.h>
#include <protos/pools.pb.h>
#include <zjcvm/zjcvm_utils.h>

namespace shardora {

namespace hotstuff {

BlockAcceptor::BlockAcceptor(
        const uint32_t& pool_idx,
        const std::shared_ptr<security::Security>& security) :
    pool_idx_(pool_idx), security_ptr_(security) {
    db_batch_ = std::make_shared<db::DbWriteBatch>();
    RegisterTxsFunc(pools::protobuf::kNormalTo,
        std::bind(&BlockAcceptor::GetToTxs, this, std::placeholders::_1));
    RegisterTxsFunc(pools::protobuf::kStatistic,
        std::bind(&BlockAcceptor::GetStatisticTxs, this, std::placeholders::_1));
    RegisterTxsFunc(pools::protobuf::kCross,
        std::bind(&BlockAcceptor::GetCrossTxs, this, std::placeholders::_1));
    RegisterTxsFunc(pools::protobuf::kConsensusRootElectShard,
        std::bind(&BlockAcceptor::GetElectTxs, this, std::placeholders::_1));
    RegisterTxsFunc(pools::protobuf::kConsensusRootTimeBlock,
        std::bind(&BlockAcceptor::GetTimeBlockTxs, this, std::placeholders::_1));
};

BlockAcceptor::~BlockAcceptor(){};

Status BlockAcceptor::Accept(std::shared_ptr<IBlockAcceptor::blockInfo>& block_info) {
    if (!block_info || !block_info->block) {
        return Status::kError;
    }
    
    if (block_info->block->pool_index() != pool_idx()) {
        return Status::kError;
    }

    if (block_info->txs.empty()) {
        // 允许不打包任何交易，但 block 必须存在
        return Status::kSuccess;
    }
    
    // Get txs from local pool
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;

    Status s = Status::kSuccess;
    s = GetTxsFromLocal(block_info, txs_ptr);
    if (s != Status::kSuccess) {
        ZJC_ERROR("invalid tx_type: %d, txs empty. pool_index: %d, view: %lu",
            block_info->tx_type, pool_idx(), block_info->view);
        return s;
    }
    
    // Do txs and create block_tx
    auto& zjc_block = block_info->block;

    std::string pool_hash = tx_pools_->latest_hash(pool_idx());
    uint64_t pool_height = tx_pools_->latest_height(pool_idx());
    if (pool_hash.empty() || pool_height == common::kInvalidUint64) {
        return Status::kError;
    }

    // replica 自己创建 block 还是 leader 传过来？传过来，也没多多少带宽。
    // 验证 zjc_block 合法性
    if (!IsBlockValid(block_info->block)) {
        return Status::kAcceptorBlockInvalid;
    }

    return DoTransactions(txs_ptr, zjc_block);
}

Status BlockAcceptor::GetTxsFromLocal(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    auto txs_func = GetTxsFunc(block_info->tx_type);
    Status s = txs_func(block_info, txs_ptr);
    if (s != Status::kSuccess) {
        return s;
    }

    if (!txs_ptr) {
        ZJC_ERROR("invalid consensus, tx empty.");
        return Status::kAcceptorTxsEmpty;
    }

    if (txs_ptr != nullptr && txs_ptr->txs.size() != block_info->txs.size()) {
        ZJC_ERROR("invalid consensus, txs not equal to leader.");
        return Status::kAcceptorTxsEmpty;
    }
    
    txs_ptr->pool_index = block_info->block->pool_index();
    return Status::kSuccess;
}

bool BlockAcceptor::IsBlockValid(const std::shared_ptr<block::protobuf::Block>&) {
    // TODO 校验 block prehash，latest height 等
    return true;
}

Status BlockAcceptor::DoTransactions(
        const std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        std::shared_ptr<block::protobuf::Block>& zjc_block) {
    // 执行交易
    auto tx_list = zjc_block->mutable_tx_list();
    auto& tx_map = txs_ptr->txs;
    tx_list->Reserve(tx_map.size());
    std::unordered_map<std::string, int64_t> acc_balance_map;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = zjc_block->height();
    zjc_host.tx_context_.block_timestamp = zjc_block->timestamp() / 1000;
    uint64_t chain_id = (((uint64_t)zjc_block->network_id()) << 32 | (uint64_t)zjc_block->pool_index());
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
            *zjc_block,
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

Status BlockAcceptor::GetDefaultTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    return Status::kSuccess;
}

Status BlockAcceptor::GetToTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = tx_pools_->GetToTxs(pool_idx(), false);
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess;
}

Status BlockAcceptor::GetStatisticTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = tx_pools_->GetStatisticTx(pool_idx(), false);
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess;
}

Status BlockAcceptor::GetCrossTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = tx_pools_->GetCrossTx(pool_idx(), false);
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess; 
}

Status BlockAcceptor::GetElectTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    if (block_info->txs.size() == 1) {
        auto txhash = pools::GetTxMessageHash(*block_info->txs[0]);
        txs_ptr = tx_pools_->GetElectTx(pool_idx(), txhash);           
    }
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess;
}

Status BlockAcceptor::GetTimeBlockTxs(
        const std::shared_ptr<IBlockAcceptor::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    txs_ptr = tx_pools_->GetTimeblockTx(pool_idx(), false);
    return !txs_ptr ? Status::kAcceptorTxsEmpty : Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

