#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class JoinElectTxItem : public TxItemBase {
public:
    JoinElectTxItem(
            const transport::MessagePtr& msg,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            std::shared_ptr<protos::PrefixDb>& prefix_db)
            : TxItemBase(msg, account_mgr, sec_ptr), prefix_db_(prefix_db) {
    }

    virtual ~JoinElectTxItem() {}
    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(JoinElectTxItem);
};

};  // namespace consensus

};  // namespace zjchain
