#pragma once

#include <common/log.h>
#include <consensus/consensus_utils.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <protos/block.pb.h>

namespace shardora {

namespace bls {
    class BlsManager;
}

namespace hotstuff {

class ViewBlockChain;
class IBlockWrapper {
public:
    IBlockWrapper() = default;
    virtual ~IBlockWrapper() {};

    virtual Status Wrap(
            const transport::MessagePtr& msg_ptr, 
            const std::shared_ptr<ViewBlock>& prev_block,
            view_block::protobuf::ViewBlockItem* view_block,
            hotstuff::protobuf::TxPropose* tx_propose,
            bool no_tx_allowed,
            std::shared_ptr<ViewBlockChain>& view_block_chain) = 0;
    virtual void GetTxSyncToLeader(
            uint32_t leader_idx, 
            uint32_t count,
            std::shared_ptr<ViewBlockChain>& view_block_chain, 
            const std::string& parent_hash,
            ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
            const std::unordered_map<std::string, uint64_t>& leader_nonce_map) = 0;
    virtual bool HasSingleTx(
        const transport::MessagePtr& msg_ptr,
        pools::CheckAddrNonceValidFunction tx_valid_func) = 0;
};

class BlockWrapper : public IBlockWrapper {
public:
    BlockWrapper(
            const uint32_t pool_idx,
            std::shared_ptr<pools::TxPoolManager>& pools_mgr,
            std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
            std::shared_ptr<block::BlockManager>& block_mgr,
            std::shared_ptr<bls::BlsManager> bls_mgr,
            const std::shared_ptr<ElectInfo>& elect_info);
    ~BlockWrapper();

    BlockWrapper(const BlockWrapper&) = delete;
    BlockWrapper& operator=(const BlockWrapper&) = delete;

    // It will change the status of the transaction and mark it as packaged
    Status Wrap(
        const transport::MessagePtr& msg_ptr, 
        const std::shared_ptr<ViewBlock>& prev_block,
        view_block::protobuf::ViewBlockItem* view_block,
        hotstuff::protobuf::TxPropose* tx_propose,
        bool no_tx_allowed,
        std::shared_ptr<ViewBlockChain>& view_block_chain) override;

    // Whether there is a built-in transaction
    bool HasSingleTx(
        const transport::MessagePtr& msg_ptr, 
        pools::CheckAddrNonceValidFunction tx_valid_func) override;
    void GetTxSyncToLeader(
            uint32_t leader_idx, 
            uint32_t count,
            std::shared_ptr<ViewBlockChain>& view_block_chain, 
            const std::string& parent_hash,
            ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs,
            const std::unordered_map<std::string, uint64_t>& leader_nonce_map) override {
        auto tx_valid_func = [&](
                const address::protobuf::AddressInfo& addr_info, 
                const pools::protobuf::TxMessage& tx_info,
                uint64_t* now_nonce) -> int {
            return CheckTransactionValid(parent_hash, view_block_chain,
                pools_mgr_,
                addr_info, tx_info, now_nonce);
        };

        txs_pools_->GetTxSyncToLeader(
            leader_idx, pool_idx_, count, 
            txs, tx_valid_func, leader_nonce_map);
    }

private:
    Status LeaderGetTxsIdempotently(
            const transport::MessagePtr& msg_ptr, 
            std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
            pools::CheckAddrNonceValidFunction tx_valid_func) {
        txs_ptr = txs_pools_->LeaderGetValidTxsIdempotently(msg_ptr, pool_idx_, tx_valid_func);
        ADD_DEBUG_PROCESS_TIMESTAMP();
        return txs_ptr != nullptr ? Status::kSuccess : Status::kWrapperTxsEmpty;
    }

  
    uint32_t pool_idx_;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> txs_pools_ = nullptr;

};

} // namespace hotstuff

} // namespace shardora
