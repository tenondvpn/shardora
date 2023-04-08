#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/contract_gas_prepayment.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace zjchain {

namespace consensus {

class ContractCall : public TxItemBase {
public:
    ContractCall(
            std::shared_ptr<ContractGasPrepayment>& prepayment,
            std::shared_ptr<db::Db>& db,
            const transport::MessagePtr& msg,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr)
            : TxItemBase(msg, account_mgr, sec_ptr) {
        prepayment_ = prepayment;
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    virtual ~ContractCall() {}
    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    int ContractExcute(
        protos::AddressInfoPtr& contract_info,
        uint64_t contract_balance,
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& tx,
        evmc::Result* out_res);
    int SaveContractCreateInfo(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& tx,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        int64_t& contract_balance_add,
        int64_t& caller_balance_add,
        int64_t& gas_more);

    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<ContractGasPrepayment> prepayment_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(ContractCall);
};

};  // namespace consensus

};  // namespace zjchain
