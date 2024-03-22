#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class FromTxItem : public TxItemBase {
public:
    FromTxItem(
            const pools::protobuf::TxMessage& tx,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr)
            : TxItemBase(tx, account_mgr, sec_ptr) {
    }

    virtual ~FromTxItem() {}
    virtual int HandleTx(
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:

    DISALLOW_COPY_AND_ASSIGN(FromTxItem);
};

};  // namespace consensus

};  // namespace shardora
