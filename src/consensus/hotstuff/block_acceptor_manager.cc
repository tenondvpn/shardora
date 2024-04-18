#include <__functional/bind.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/block_acceptor_manager.h>
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
    // TODO Get txs from local pool
    std::shared_ptr<consensus::WaitingTxsItem> txs_ptr = nullptr;
    if (block_info->txs.size() <= 0) {
        return Status::kSuccess;
    }
    
    auto txs_func = GetTxsFunc(block_info->tx_type);
    Status s = txs_func(txs_ptr);
    if (s != Status::kSuccess) {
        return s;
    }
    
    // TODO Do txs and create block_tx
    auto& zjc_block = block_info->block;

    // TODO verify txs result hash
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

