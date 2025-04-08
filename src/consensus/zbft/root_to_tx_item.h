#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace vss {
    class VssManager;
};

namespace consensus {

class RootToTxItem : public TxItemBase {
public:
    RootToTxItem(
        uint32_t max_consensus_sharding_id,
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info);
    virtual ~RootToTxItem();

    virtual int TxToBlockTx(
            const pools::protobuf::TxMessage& tx_info,
            block::protobuf::BlockTx* block_tx) {
        if (!DefaultTxItem(tx_info, block_tx)) {
            return consensus::kConsensusError;
        }

        return consensus::kConsensusSuccess;
    }

    virtual int HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:

    uint32_t max_sharding_id_ = 0;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    
    DISALLOW_COPY_AND_ASSIGN(RootToTxItem);
};

};  // namespace consensus

};  // namespace shardora
