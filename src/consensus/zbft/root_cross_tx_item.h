#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class RootCrossTxItem : public TxItemBase {
public:
    RootCrossTxItem(
        const transport::MessagePtr& msg_ptr,
        int32_t tx_index,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg_ptr, tx_index, account_mgr, sec_ptr, addr_info) {}
    virtual ~RootCrossTxItem() {}

private:
    DISALLOW_COPY_AND_ASSIGN(RootCrossTxItem);
};

};  // namespace consensus

};  // namespace shardora
