#pragma once

#include <common/log.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>

namespace shardora {

namespace hotstuff {

class IBlockWrapper {
public:
    IBlockWrapper() = default;
    virtual ~IBlockWrapper() {};

    virtual Status Wrap(
            const std::shared_ptr<block::protobuf::Block>& prev_block,
            const uint32_t& leader_idx,
            std::shared_ptr<block::protobuf::Block>& block,
            std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose) = 0;
    virtual Status GetTxsIdempotently(std::vector<std::shared_ptr<pools::protobuf::TxMessage>>& txs) = 0;
};

class BlockWrapper : public IBlockWrapper {
public:
    // 允许没有交易成功打包的次数，需要连续 3 个 QC 才能提交
    // TODO 如果中间出现超时，则需要不止三个块
    static const uint32_t NO_TX_ALLOWED_TIMES = 0; 
    
    BlockWrapper(
            const uint32_t pool_idx,
            std::shared_ptr<pools::TxPoolManager>& pools_mgr,
            std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
            std::shared_ptr<block::BlockManager>& block_mgr,
            const std::shared_ptr<ElectInfo>& elect_info);
    ~BlockWrapper();

    BlockWrapper(const BlockWrapper&) = delete;
    BlockWrapper& operator=(const BlockWrapper&) = delete;

    // 会改变交易的状态，标记已打包
    Status Wrap(
            const std::shared_ptr<block::protobuf::Block>& prev_block,
            const uint32_t& leader_idx,
            std::shared_ptr<block::protobuf::Block>& block,
            std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose) override;

    // 幂等，用于同步 replica 向 leader 同步交易
    Status GetTxsIdempotently(std::vector<std::shared_ptr<pools::protobuf::TxMessage>>& txs) override;

private:
    uint32_t pool_idx_;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> txs_pools_ = nullptr;
    uint32_t times_of_no_txs_ = 0; // 没有交易的次数

    Status PopTxs(std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
        pools_mgr_->PopTxs(pool_idx_, false);
        pools_mgr_->CheckTimeoutTx(pool_idx_);
        
        txs_ptr = txs_pools_->LeaderGetValidTxs(pool_idx_);
        ZJC_DEBUG("====3 pool: %d pop txs: %lu", pool_idx_, txs_ptr->txs.size());
        return txs_ptr != nullptr ? Status::kSuccess : Status::kWrapperTxsEmpty;
    }
};

} // namespace hotstuff

} // namespace shardora

