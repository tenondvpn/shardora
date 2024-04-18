#include <__functional/bind.h>
#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/block_acceptor_manager.h>
#include <consensus/hotstuff/types.h>
#include <protos/pools.pb.h>

namespace shardora {

namespace hotstuff {

BlockAcceptorManager::BlockAcceptorManager() {
    RegisterTxsFunc(pools::protobuf::kNormalTo, std::bind(&BlockAcceptorManager::GetToTxs, this, std::placeholders::_1));
    RegisterTxsFunc(pools::protobuf::kStatistic, std::bind(&BlockAcceptorManager::GetStatisticTxs, this, std::placeholders::_1));
    RegisterTxsFunc(pools::protobuf::kCross, std::bind(&BlockAcceptorManager::GetCrossTxs, this, std::placeholders::_1));
    RegisterTxsFunc(pools::protobuf::kConsensusRootElectShard, std::bind(&BlockAcceptorManager::GetElectTxs, this, std::placeholders::_1));
    RegisterTxsFunc(pools::protobuf::kConsensusRootTimeBlock, std::bind(&BlockAcceptorManager::GetTimeBlockTxs, this, std::placeholders::_1));
};

BlockAcceptorManager::~BlockAcceptorManager(){};

Status BlockAcceptorManager::Accept(std::shared_ptr<IBlockAcceptorManager::blockInfo>& block_info) {
    if (!block_info || !block_info->block || block_info->txs.empty()) {
        return Status::kSuccess;
    }
    // TODO Get txs from local pool
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;

    Status s = Status::kSuccess;
    s = GetTxsFromLocal(block_info, txs_ptr);
    if (s != Status::kSuccess) {
        return s;
    }
    
    // TODO Do txs and create block_tx
    auto& zjc_block = block_info->block;

    std::string pool_hash = tx_pools_->latest_hash(block_info->block->pool_index());
    uint64_t pool_height = tx_pools_->latest_height(block_info->block->pool_index());
    if (pool_hash.empty() || pool_height == common::kInvalidUint64) {
        return Status::kError;
    }

    // replica 自己创建 block 还是 leader 传过来？传过来，也没多多少带宽。
    // TODO 验证 zjc_block 合法性
    if (!IsBlockValid(block_info->block)) {
        return Status::kAcceptorBlockInvalid;
    }

    // 执行交易
    

    // TODO verify txs result hash
    return Status::kSuccess;
}

Status BlockAcceptorManager::GetTxsFromLocal(
        const std::shared_ptr<IBlockAcceptorManager::blockInfo>& block_info,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
    auto txs_func = GetTxsFunc(block_info->tx_type);
    Status s = txs_func(txs_ptr);
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

} // namespace hotstuff

} // namespace shardora

