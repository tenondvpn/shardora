#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace zjchain {

namespace contract {
    class ContractManager;
};

namespace consensus {

class ContractUserCreateCall : public TxItemBase {
public:
    ContractUserCreateCall(
            std::shared_ptr<contract::ContractManager>& contract_mgr,
            std::shared_ptr<db::Db>& db,
            const transport::MessagePtr& msg,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr)
            : TxItemBase(msg, account_mgr, sec_ptr) {
        contract_mgr_ = contract_mgr;
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    virtual ~ContractUserCreateCall() {}
    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    int CreateContractCallExcute(
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

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(ContractUserCreateCall);
};

};  // namespace consensus

};  // namespace zjchain
