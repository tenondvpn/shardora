#pragma once

#include "common/bloom_filter.h"
#include "consensus/consensus_utils.h"
#include "network/network_utils.h"
#include "pools/tx_pool_manager.h"

namespace zjchain {

namespace consensus {

struct WaitingTxsItem {
    WaitingTxsItem()
        : bloom_filter(nullptr),
        max_txs_hash_count(0),
        tx_type(pools::protobuf::kNormalFrom) {}
    std::string all_txs_hash;
    std::map<std::string, pools::TxItemPtr> txs;
    std::shared_ptr<common::BloomFilter> bloom_filter;
    std::unordered_map<std::string, uint32_t> all_hash_count;
    std::string max_txs_hash;
    uint32_t max_txs_hash_count;
    uint32_t pool_index;
    uint8_t thread_index;
    pools::protobuf::StepType tx_type;
};

class WaitingTxs {
public:
    WaitingTxs() {}
    ~WaitingTxs() {}

    void Init(uint32_t pool_index, std::shared_ptr<pools::TxPoolManager>& pools_mgr) {
        pool_index_ = pool_index;
        pools_mgr_ = pools_mgr;
    }

    std::shared_ptr<WaitingTxsItem> LeaderGetValidTxs(bool direct) {
        if (direct) {
            return DirectGetValidTxs();
        }

        return LeaderGetTxs();
    }

    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(const common::BloomFilter& bloom_filter) {
        txs_items_ = std::make_shared<WaitingTxsItem>();
        pools_mgr_->GetTx(bloom_filter, pool_index_, txs_items_->txs);
        auto& all_txs_hash = txs_items_->all_txs_hash;
        for (auto iter = txs_items_->txs.begin(); iter != txs_items_->txs.end(); ++iter) {
            all_txs_hash.append(iter->first);
        }

        all_txs_hash = common::Hash::keccak256(all_txs_hash);
        if (txs_items_->txs.empty()) {
            txs_items_ = nullptr;
        }

        return txs_items_;
    }

    std::shared_ptr<WaitingTxsItem> FollowerGetTxs(
            const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list) {
        txs_items_ = std::make_shared<WaitingTxsItem>();
        auto& tx_map = txs_items_->txs;
        for (int32_t i = 0; i < tx_hash_list.size(); ++i) {
            auto tx_item = pools_mgr_->GetTx(pool_index_, tx_hash_list[i]);
            if (tx_item != nullptr) {
                tx_map[tx_hash_list[i]] = tx_item;
            }
        }

        auto& all_txs_hash = txs_items_->all_txs_hash;
        for (auto iter = txs_items_->txs.begin(); iter != txs_items_->txs.end(); ++iter) {
            all_txs_hash.append(iter->first);
        }

        all_txs_hash = common::Hash::keccak256(all_txs_hash);
        if (txs_items_->txs.empty()) {
            txs_items_ = nullptr;
        }

        return txs_items_;
    }

private:
    std::shared_ptr<WaitingTxsItem> DirectGetValidTxs() {
        auto tx_items_ptr = std::make_shared<WaitingTxsItem>();
        auto& tx_vec = tx_items_ptr->txs;
        pools_mgr_->GetTx(pool_index_, kMaxTxCount, tx_vec);
        if (tx_vec.empty()) {
            return nullptr;
        }

        return tx_items_ptr;
    }

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