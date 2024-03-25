#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace shardora {

namespace consensus {

class ContractUserCall : public TxItemBase {
public:
    ContractUserCall(
            std::shared_ptr<db::Db>& db,
            const pools::protobuf::TxMessage& msg,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg, account_mgr, sec_ptr, addr_info) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    virtual ~ContractUserCall() {}
    virtual int HandleTx(
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ContractUserCall);
};

};  // namespace consensus

};  // namespace shardora
