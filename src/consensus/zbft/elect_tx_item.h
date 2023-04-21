#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class ElectTxItem : public TxItemBase {
public:
    ElectTxItem(
        const transport::MessagePtr& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr)
        : TxItemBase(msg, account_mgr, sec_ptr) {}
    virtual ~ElectTxItem() {}
    virtual int HandleTx(
            uint8_t thread_idx,
            const block::protobuf::Block& block,
            std::shared_ptr<db::DbWriteBatch>& db_batch,
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        return kConsensusError;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(ElectTxItem);
};

};  // namespace consensus

};  // namespace zjchain
