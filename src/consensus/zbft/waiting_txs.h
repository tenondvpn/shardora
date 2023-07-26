#pragma once

#include "common/bloom_filter.h"
#include "consensus/consensus_utils.h"
#include "network/network_utils.h"
#include "pools/tx_pool_manager.h"

namespace zjchain {

namespace consensus {

class WaitingTxs {
public:
    WaitingTxs() {}
    ~WaitingTxs() {}

    void Init(uint32_t pool_index, std::shared_ptr<pools::TxPoolManager>& pools_mgr) {
        pool_index_ = pool_index;
        pools_mgr_ = pools_mgr;
    }

    std::shared_ptr<WaitingTxsItem> LeaderGetValidTxs() {
        return LeaderGetTxs();
    }

    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list,
            const std::set<uint8_t>& leader_invalid_tx,
            std::vector<uint8_t>* invalid_txs) {
        txs_items_ = pools_mgr_->GetTx(pool_index_, tx_hash_list, leader_invalid_tx, invalid_txs);
        if (txs_items_ != nullptr && txs_items_->txs.empty()) {
            txs_items_ = nullptr;
        }

        return txs_items_;
    }

private:
    std::shared_ptr<WaitingTxsItem> LeaderGetTxs() {
        txs_items_ = std::make_shared<WaitingTxsItem>();
        auto& tx_vec = txs_items_->txs;
        if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
            pools_mgr_->GetTx(pool_index_, 1, tx_vec);
        } else {
            pools_mgr_->GetTx(pool_index_, kMaxTxCount, tx_vec);
        }

        if (txs_items_->txs.empty()) {
            txs_items_ = nullptr;
        }

        return txs_items_;
    }

    uint32_t pool_index_ = 0;
    std::shared_ptr<WaitingTxsItem> txs_items_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(WaitingTxs);
};

}  // namespace consensus

}  // namespace zjchain