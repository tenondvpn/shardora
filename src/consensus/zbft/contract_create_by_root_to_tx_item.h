#pragma once

#include <consensus/zbft/tx_item_base.h>
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace shardora {

namespace consensus {

// For kContractCreateByRootTo Tx
class ContractCreateByRootToTxItem : public TxItemBase {
public:
	ContractCreateByRootToTxItem(std::shared_ptr<contract::ContractManager>& contract_mgr,
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

	virtual ~ContractCreateByRootToTxItem() {}
	virtual int HandleTx(
            const view_block::protobuf::ViewBlockItem& view_block,
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
                int64_t& contract_balance_add,
                int64_t& caller_balance_add,
                int64_t& gas_more);
	std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(ContractCreateByRootToTxItem);
};

}; // namespace consensus

}; // namespace shardora

