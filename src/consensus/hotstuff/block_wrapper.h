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
            const transport::MessagePtr& msg_ptr, 
            const std::shared_ptr<ViewBlock>& prev_block,
            const uint32_t& leader_idx,
            view_block::protobuf::ViewBlockItem* view_block,
            hotstuff::protobuf::TxPropose* tx_propose,
            const bool& no_tx_allowed,
            std::shared_ptr<ViewBlockChain>& view_block_chain) = 0;
    virtual void GetTxSyncToLeader(
            uint32_t leader_idx, 
            std::shared_ptr<ViewBlockChain>& view_block_chain, 
            const std::string& parent_hash,
            ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs) = 0;
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
        const transport::MessagePtr& msg_ptr, 
        const std::shared_ptr<ViewBlock>& prev_block,
        const uint32_t& leader_idx,
        view_block::protobuf::ViewBlockItem* view_block,
        hotstuff::protobuf::TxPropose* tx_propose,
        const bool& no_tx_allowed,
        std::shared_ptr<ViewBlockChain>& view_block_chain) override;

    // 是否存在内置交易
    bool HasSingleTx(pools::CheckGidValidFunction gid_valid_fn) override;
    void GetTxSyncToLeader(
            uint32_t leader_idx, 
            std::shared_ptr<ViewBlockChain>& view_block_chain, 
            const std::string& parent_hash,
            ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs) override {
        auto gid_valid_func = [&](const std::string& gid) -> bool {
            return view_block_chain->CheckTxGidValid(gid, parent_hash);
        };

        txs_pools_->GetTxSyncToLeader(pool_idx_, consensus::kSyncToLeaderTxCount, txs, gid_valid_func);
    }

private:
    Status LeaderGetTxsIdempotently(
            const transport::MessagePtr& msg_ptr, 
            std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
            pools::CheckGidValidFunction gid_vlid_func) {
        txs_ptr = txs_pools_->LeaderGetValidTxsIdempotently(msg_ptr, pool_idx_, gid_vlid_func);
        ADD_DEBUG_PROCESS_TIMESTAMP();
        return txs_ptr != nullptr ? Status::kSuccess : Status::kWrapperTxsEmpty;
    }

  
    uint32_t pool_idx_;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> txs_pools_ = nullptr;

};

} // namespace hotstuff

} // namespace shardora

