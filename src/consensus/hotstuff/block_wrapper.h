#pragma once

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
};

class BlockWrapper : public IBlockWrapper {
public:
    BlockWrapper(
            const uint32_t pool_idx,
            const std::shared_ptr<pools::TxPoolManager>& pools_mgr,
            const std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
            const std::shared_ptr<block::BlockManager>& block_mgr,
            const std::shared_ptr<ElectInfo>& elect_info);
    ~BlockWrapper();

    BlockWrapper(const BlockWrapper&) = delete;
    BlockWrapper& operator=(const BlockWrapper&) = delete;

    Status Wrap(
            const std::shared_ptr<block::protobuf::Block>& prev_block,
            const uint32_t& leader_idx,
            std::shared_ptr<block::protobuf::Block>& block,
            std::shared_ptr<hotstuff::protobuf::TxPropose>& tx_propose) override;

private:
    uint32_t pool_idx_;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> txs_pools_ = nullptr;

    Status GetTxs(std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr) {
        txs_ptr = txs_pools_->LeaderGetValidTxs(pool_idx_);
        return txs_ptr != nullptr ? Status::kSuccess : Status::kError;
    }
};

} // namespace hotstuff

} // namespace shardora

