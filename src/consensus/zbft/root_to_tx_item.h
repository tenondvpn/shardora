#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace zjchain {

namespace vss {
    class VssManager;
};

namespace consensus {

class RootToTxItem : public TxItemBase {
public:
    RootToTxItem(
        uint32_t max_consensus_sharding_id,
        const transport::MessagePtr& msg,
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr);
    virtual ~RootToTxItem();

    virtual int HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx);

private:

    uint32_t max_sharding_id_ = 0;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    
    DISALLOW_COPY_AND_ASSIGN(RootToTxItem);
};

};  // namespace consensus

};  // namespace zjchain
