#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class FromTxItem : public TxItemBase {
public:
    FromTxItem(
            const transport::MessagePtr& msg_ptr,
            int32_t tx_index,
            std::shared_ptr<block::AccountManager>& account_mgr,
            std::shared_ptr<security::Security>& sec_ptr,
            protos::AddressInfoPtr& addr_info)
            : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {
        common::GlobalInfo::Instance()->AddSharedObj(4);
    }

    virtual ~FromTxItem() {
        common::GlobalInfo::Instance()->DecSharedObj(4);
    }

    virtual int HandleTx(
        view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:

    DISALLOW_COPY_AND_ASSIGN(FromTxItem);
};

};  // namespace consensus

};  // namespace shardora
