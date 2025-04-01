#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/contract_gas_prepayment.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace shardora {

namespace contract {
    class ContractManager;
};

namespace consensus {

class ContractCall : public TxItemBase {
public:
    ContractCall(
            std::shared_ptr<contract::ContractManager>& contract_mgr,
            std::shared_ptr<ContractGasPrepayment>& prepayment,
            std::shared_ptr<db::Db>& db,
            const transport::MessagePtr& msg_ptr,
            int32_t tx_index,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {
        contract_mgr_ = contract_mgr;
        prepayment_ = prepayment;
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    virtual ~ContractCall() {}
    virtual int HandleTx(
        const view_block::protobuf::ViewBlockItem& block,
        zjcvm::ZjchainHost& zjchost,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    int ContractExcute(
        protos::AddressInfoPtr& contract_info,
        uint64_t contract_balance,
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& tx,
        uint64_t gas_limit,
        evmc::Result* out_res);
    int SaveContractCreateInfo(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::BlockTx& tx,
        int64_t& contract_balance_add,
        int64_t& caller_balance_add,
        int64_t& gas_more);
    void GetTempPerpaymentBalance(
        const view_block::protobuf::ViewBlockItem& block,
        const block::protobuf::BlockTx& block_tx,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        uint64_t* balance);

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<ContractGasPrepayment> prepayment_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(ContractCall);
};

};  // namespace consensus

};  // namespace shardora
