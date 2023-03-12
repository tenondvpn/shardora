#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class ToTxLocalItem : public TxItemBase {
public:
    ToTxLocalItem(
            transport::MessagePtr& msg,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr)
            : TxItemBase(msg, account_mgr, sec_ptr), db_(db) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    }


    virtual ~ToTxLocalItem() {}
    virtual int HandleTx(
        uint8_t thread_idx,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx);

private:
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ToTxLocalItem);
};

};  // namespace consensus

};  // namespace zjchain




