#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "shardoravm/shardora_host.h"
#include "shardoravm/shardoravm_utils.h"

namespace shardora {

namespace contract {
    class ContractManager;
};

namespace consensus {

class ContractCall : public TxItemBase {
public:
    ContractCall(
            std::shared_ptr<contract::ContractManager>& contract_mgr,
            std::shared_ptr<db::Db>& db,
            const transport::MessagePtr& msg_ptr,
            int32_t tx_index,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {
        contract_mgr_ = contract_mgr;
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    virtual ~ContractCall() {}
    virtual int HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& block,
        shardoravm::ShardorahainHost& shardorahost,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    int ContractExcute(
        protos::AddressInfoPtr& contract_info,
        uint64_t contract_balance,
        shardoravm::ShardorahainHost& shardora_host,
        block::protobuf::BlockTx& tx,
        uint64_t gas_limit,
        evmc::Result* out_res);
    int SaveContractCreateInfo(
        shardoravm::ShardorahainHost& shardora_host,
        block::protobuf::BlockTx& tx,
        hotstuff::BalanceAndNonceMap& dep_contract_balance_map,
        int64_t& contract_balance_add);

    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::map<std::string, std::shared_ptr<pools::protobuf::ToTxMessageItem>> cross_to_map_;
    DISALLOW_COPY_AND_ASSIGN(ContractCall);
};

};  // namespace consensus

};  // namespace shardora
