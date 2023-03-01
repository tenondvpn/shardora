#pragma once

#include <memory>
#include <map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <set>
#include <deque>
#include <queue>

#include "common/bloom_filter.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/time_utils.h"
#include "common/user_property_key_define.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"
#include "pools/tx_utils.h"
#include "protos/pools.pb.h"

namespace zjchain {

namespace pools {

struct TxItemPriOper {
    bool operator() (TxItemPtr& a, TxItemPtr& b) {
        return a->gas_price < b->gas_price;
    }
};

class TxPool {
public:
    TxPool();
    ~TxPool();
    void Init(uint32_t pool_idx);
    int AddTx(TxItemPtr& tx_ptr);
//     bool IsPrevTxsOver() {
//         return waiting_txs_.empty();
//     }

    TxItemPtr GetTx();
    TxItemPtr GetTx(const std::string& sgid);
    void GetTx(
        const common::BloomFilter& bloom_filter,
        std::map<std::string, TxItemPtr>& res_map);
    void TxOver(std::map<std::string, TxItemPtr>& txs);
    void TxRecover(std::map<std::string, TxItemPtr>& txs);
    uint64_t latest_height() const {
        return latest_height_;
    }

    std::string latest_hash() const {
        return latest_hash_;
    }

private:
    bool CheckTimeoutTx(TxItemPtr& tx_ptr, uint64_t timestamp_now);

    std::priority_queue<TxItemPtr, std::vector<TxItemPtr>, TxItemPriOper> mem_queue_;
//     std::set<std::string> waiting_txs_;
    std::deque<TxItemPtr> timeout_txs_;
    std::unordered_map<std::string, TxItemPtr> added_tx_map_;
    uint32_t pool_index_ = 0;
    uint64_t latest_height_ = 0;
    std::string latest_hash_;

    DISALLOW_COPY_AND_ASSIGN(TxPool);
};

}  // namespace pools

}  // namespace zjchain
