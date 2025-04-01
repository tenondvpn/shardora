#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class PoolStatisticTag : public TxItemBase {
public:
    PoolStatisticTag(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {}
    virtual ~PoolStatisticTag() {}
    virtual int HandleTx(
            const view_block::protobuf::ViewBlockItem& view_block,
            zjcvm::ZjchainHost& zjc_host,
            hotstuff::BalanceAndNonceMap& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        uint64_t to_balance = 0;
        uint64_t to_nonce = 0;
        GetTempAccountBalance(block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
        if (to_nonce + 1 != block_tx.nonce()) {
            block_tx.set_status(kConsensusNonceInvalid);
            ZJC_WARN("failed call pool statistic tag: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
                view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
            return consensus::kConsensusSuccess;
        }

        ZJC_WARN("failed call pool statistic tag: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
            view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
        return consensus::kConsensusSuccess;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(PoolStatisticTag);
};

};  // namespace consensus

};  // namespace shardora
