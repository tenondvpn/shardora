#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class CrossTxItem : public TxItemBase {
public:
    CrossTxItem(
        const transport::MessagePtr& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr)
        : TxItemBase(msg, account_mgr, sec_ptr) {}
    virtual ~CrossTxItem() {}

private:
    DISALLOW_COPY_AND_ASSIGN(CrossTxItem);
};

};  // namespace consensus

};  // namespace zjchain
