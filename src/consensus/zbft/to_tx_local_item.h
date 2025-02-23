#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "consensus/zbft/contract_gas_prepayment.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class ToTxLocalItem : public TxItemBase {
public:
    ToTxLocalItem(
            const pools::protobuf::TxMessage& msg,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<ContractGasPrepayment>& gas_prepayment,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg, account_mgr, sec_ptr, addr_info), db_(db) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        gas_prepayment_ = gas_prepayment;
    }


    virtual ~ToTxLocalItem() {}
    virtual int HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);
    virtual int TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx);

private:
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<ContractGasPrepayment> gas_prepayment_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ToTxLocalItem);
};

};  // namespace consensus

};  // namespace shardora




