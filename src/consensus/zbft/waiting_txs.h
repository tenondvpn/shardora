#pragma once

#include "common/bloom_filter.h"
#include "consensus/consensus_utils.h"
#include "network/network_utils.h"
#include "pools/tx_pool_manager.h"
#include <common/log.h>

namespace shardora {

namespace consensus {

class WaitingTxs {
public:
    WaitingTxs() {}
    ~WaitingTxs() {}

    void Init(uint32_t pool_index, std::shared_ptr<pools::TxPoolManager>& pools_mgr) {
        pool_index_ = pool_index;
        pools_mgr_ = pools_mgr;
    }

    std::shared_ptr<WaitingTxsItem> LeaderGetValidTxsIdempotently(pools::CheckGidValidFunction gid_vlid_func) {
        return LeaderGetTxsIdempotently(gid_vlid_func);
    }

private:
    std::shared_ptr<WaitingTxsItem> LeaderGetTxs() {
        auto txs_items = std::make_shared<WaitingTxsItem>();
        auto& tx_vec = txs_items->txs;
        if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
            pools_mgr_->GetTx(pool_index_, 1, tx_vec);
        } else {
            pools_mgr_->GetTx(pool_index_, kMaxTxCount, tx_vec);
        }

        if (txs_items->txs.empty()) {
            txs_items = nullptr;
        }

        return txs_items;
    }

    std::shared_ptr<WaitingTxsItem> LeaderGetTxsIdempotently(pools::CheckGidValidFunction gid_vlid_func) {
        transport::protobuf::Header header;
        auto txs_items = std::make_shared<WaitingTxsItem>();
        auto& tx_vec = txs_items->txs;
        std::map<std::string, pools::TxItemPtr> invalid_txs;
        if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
            pools_mgr_->GetTxIdempotently(pool_index_, 1, tx_vec, gid_vlid_func);
        } else {
            pools_mgr_->GetTxIdempotently(pool_index_, kMaxTxCount, tx_vec, gid_vlid_func);
        }

        if (txs_items->txs.empty()) {
            txs_items = nullptr;
        }

        return txs_items;
    }    

    uint32_t pool_index_ = 0;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(WaitingTxs);
};

}  // namespace consensus

}  // namespace shardora
