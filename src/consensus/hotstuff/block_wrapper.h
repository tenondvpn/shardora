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
            const std::shared_ptr<ElectInfo>& elect_info);
    ~BlockWrapper();

    BlockWrapper(const BlockWrapper&) = delete;
    BlockWrapper& operator=(const BlockWrapper&) = delete;

    // It will change the status of the transaction and mark it as packaged
    Status Wrap(
        const transport::MessagePtr& msg_ptr, 
        const std::shared_ptr<ViewBlock>& prev_block,
        const uint32_t& leader_idx,
        view_block::protobuf::ViewBlockItem* view_block,
        hotstuff::protobuf::TxPropose* tx_propose,
        const bool& no_tx_allowed,
        std::shared_ptr<ViewBlockChain>& view_block_chain) override;

    // Whether there is a built-in transaction
    bool HasSingleTx(
        const transport::MessagePtr& msg_ptr, 
        pools::CheckAddrNonceValidFunction tx_valid_func) override;
    void GetTxSyncToLeader(
            uint32_t leader_idx, 
            std::shared_ptr<ViewBlockChain>& view_block_chain, 
            const std::string& parent_hash,
            ::google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>* txs) override {
        auto tx_valid_func = [&](
                const address::protobuf::AddressInfo& addr_info, 
                pools::protobuf::TxMessage& tx_info) -> bool {
            if (pools::IsUserTransaction(tx_info.step())) {
                return view_block_chain->CheckTxNonceValid(
                    addr_info.addr(), 
                    tx_info.nonce(), 
                    parent_hash);
            }
            
            zjcvm::ZjchainHost zjc_host;
            zjc_host.parent_hash_ = parent_hash;
            zjc_host.view_block_chain_ = view_block_chain;
            std::string val;
            if (zjc_host.GetKeyValue(tx_info.to(), tx_info.key(), &val) == zjcvm::kZjcvmSuccess) {
                SHARDORA_DEBUG("not user tx unique hash exists to: %s, unique hash: %s, step: %d",
                    common::Encode::HexEncode(tx_info.to()).c_str(),
                    common::Encode::HexEncode(tx_info.key()).c_str(),
                    (int32_t)tx_info.step());
                return 1;
            }

            SHARDORA_DEBUG("not user tx unique hash success to: %s, unique hash: %s",
                common::Encode::HexEncode(tx_info.to()).c_str(),
                common::Encode::HexEncode(tx_info.key()).c_str());
            return 0;
        };

        txs_pools_->GetTxSyncToLeader(
            leader_idx, pool_idx_, consensus::kSyncToLeaderTxCount, 
            txs, tx_valid_func);
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
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> txs_pools_ = nullptr;

};

} // namespace hotstuff

} // namespace shardora
