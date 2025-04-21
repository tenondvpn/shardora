#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class ToTxLocalItem : public TxItemBase {
public:
    ToTxLocalItem(
            const transport::MessagePtr& msg_ptr,
            int32_t tx_index,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info), db_(db) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    }


    virtual ~ToTxLocalItem() {}
    virtual int HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx);
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx);

private:
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::string unique_hash_;

    DISALLOW_COPY_AND_ASSIGN(ToTxLocalItem);
};

};  // namespace consensus

};  // namespace shardora




