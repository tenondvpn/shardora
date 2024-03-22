#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class StatisticTxItem : public TxItemBase {
public:
    StatisticTxItem(
        const transport::MessagePtr& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr)
        : TxItemBase(msg, account_mgr, sec_ptr) {}
    virtual ~StatisticTxItem() {}

private:
    DISALLOW_COPY_AND_ASSIGN(StatisticTxItem);
};

};  // namespace consensus

};  // namespace shardora
