#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class PoolStatisticTag : public TxItemBase {
public:
    PoolStatisticTag(
        const pools::protobuf::TxMessage* msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg, account_mgr, sec_ptr, addr_info) {}
    virtual ~PoolStatisticTag() {}
    virtual int HandleTx(
            const view_block::protobuf::ViewBlockItem& view_block,
            zjcvm::ZjchainHost& zjc_host,
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        ZJC_WARN("succcess call pool statistic tag pool: %d, view: %lu", 
            view_block.qc().pool_index(), view_block.qc().view());
        return consensus::kConsensusSuccess;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(PoolStatisticTag);
};

};  // namespace consensus

};  // namespace shardora
