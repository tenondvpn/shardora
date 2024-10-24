#pragma once

#include <common/log.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>

namespace shardora {

namespace hotstuff {

class ViewBlockChain;
class IBlockWrapper {
public:
    IBlockWrapper() = default;
    virtual ~IBlockWrapper() {};

    virtual Status Wrap(
            const std::shared_ptr<ViewBlock>& prev_block,
            const uint32_t& leader_idx,
            view_block::protobuf::ViewBlockItem* view_block,
            hotstuff::protobuf::TxPropose* tx_propose,
            const bool& no_tx_allowed,
            std::shared_ptr<ViewBlockChain>& view_block_chain) = 0;
    virtual Status GetTxsIdempotently(std::vector<std::shared_ptr<pools::protobuf::TxMessage>>& txs) = 0;
    virtual bool HasSingleTx(pools::CheckGidValidFunction gid_valid_fn) = 0;
};

class BlockWrapper : public IBlockWrapper {
public:
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
            const std::shared_ptr<ViewBlock>& prev_block,
            const uint32_t& leader_idx,
            view_block::protobuf::ViewBlockItem* view_block,
            hotstuff::protobuf::TxPropose* tx_propose,
            const bool& no_tx_allowed,
            std::shared_ptr<ViewBlockChain>& view_block_chain) override;

    // 幂等，用于同步 replica 向 leader 同步交易
    Status GetTxsIdempotently(std::vector<std::shared_ptr<pools::protobuf::TxMessage>>& txs) override;
    // 是否存在内置交易
    bool HasSingleTx(pools::CheckGidValidFunction gid_valid_fn) override;

private:
    uint32_t pool_idx_;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> txs_pools_ = nullptr;

    Status LeaderGetTxsIdempotently(
            std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
            pools::CheckGidValidFunction gid_vlid_func) {
        pools_mgr_->PopTxs(pool_idx_, false, nullptr, nullptr);
        pools_mgr_->CheckTimeoutTx(pool_idx_);
        
        txs_ptr = txs_pools_->LeaderGetValidTxsIdempotently(pool_idx_, gid_vlid_func);
        return txs_ptr != nullptr ? Status::kSuccess : Status::kWrapperTxsEmpty;
    }
};

} // namespace hotstuff

} // namespace shardora

