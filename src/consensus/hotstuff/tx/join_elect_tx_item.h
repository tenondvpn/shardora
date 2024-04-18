#pragma once

#include "block/account_manager.h"
#include "tx_item_base.h"
#include "elect/elect_manager.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class JoinElectTxItem : public TxItemBase {
public:
    JoinElectTxItem(
            const pools::protobuf::TxMessage& msg,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            std::shared_ptr<protos::PrefixDb>& prefix_db,
            std::shared_ptr<elect::ElectManager>& elect_mgr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg, account_mgr, sec_ptr, addr_info), prefix_db_(prefix_db), elect_mgr_(elect_mgr) {
    }

    virtual ~JoinElectTxItem() {}
    virtual int HandleTx(
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(JoinElectTxItem);
};

};  // namespace consensus

};  // namespace shardora
