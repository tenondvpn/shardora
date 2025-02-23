#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace shardora {

namespace consensus {

class TimeBlockTx : public TxItemBase {
public:
    TimeBlockTx(
        const pools::protobuf::TxMessage& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        protos::AddressInfoPtr& addr_info)
        : TxItemBase(msg, account_mgr, sec_ptr, addr_info) {}
    virtual ~TimeBlockTx() {}

private:
    DISALLOW_COPY_AND_ASSIGN(TimeBlockTx);
};

};  // namespace consensus

};  // namespace shardora
