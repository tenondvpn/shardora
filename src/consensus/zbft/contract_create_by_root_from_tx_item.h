#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace shardora {
namespace consensus {

class ContractCreateByRootFromTxItem : public TxItemBase {
public:
    ContractCreateByRootFromTxItem(
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

    virtual ~ContractCreateByRootFromTxItem() {}
    virtual int HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(ContractCreateByRootFromTxItem);
};

};  // namespace consensus

};  // namespace shardora
