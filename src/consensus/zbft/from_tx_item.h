#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class FromTxItem : public TxItemBase {
public:
    FromTxItem(
            const pools::protobuf::TxMessage* tx,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(tx, account_mgr, sec_ptr, addr_info) {
    }

    virtual ~FromTxItem() {}
    virtual int HandleTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:

    DISALLOW_COPY_AND_ASSIGN(FromTxItem);
};

};  // namespace consensus

};  // namespace shardora
