#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "protos/prefix_db.h"
#include "security/security.h"
#include "shardoravm/shardora_host.h"
#include "shardoravm/shardoravm_utils.h"

namespace shardora {

namespace consensus {

class ContractPrefund : public TxItemBase {
public:
    ContractPrefund(
            std::shared_ptr<db::Db>& db,
            const transport::MessagePtr& msg_ptr,
            int32_t tx_index,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    virtual ~ContractPrefund() {}
    virtual int HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ContractPrefund);
};

};  // namespace consensus

};  // namespace shardora
