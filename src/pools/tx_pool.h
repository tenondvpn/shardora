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
#include "pools/tx_utils.h"
#include "protos/pools.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace pools {

struct TxItem {
    TxItem(transport::MessagePtr& msg) : msg_ptr(msg) {
        time_valid = common::TimeUtils::TimestampUs() + kBftStartDeltaTime;
#ifdef ZJC_UNITTEST
        time_valid = 0;
#endif // ZJC_UNITTEST
        timeout = common::TimeUtils::TimestampUs() + kTxPoolTimeoutUs;
        remove_timeout = timeout + kTxPoolTimeoutUs;
        gas_price = msg->header.tx_proto().gas_price();
        if (msg->header.tx_proto().has_step()) {
            step = msg->header.tx_proto().step();
        }

        sgid = common::Hash::keccak256(
            msg->header.tx_proto().gid() + std::to_string(step) + msg->msg_hash);
        tx_hash = sgid;
    }
    
    transport::MessagePtr msg_ptr;
    uint64_t timeout;
    uint64_t remove_timeout;
    uint64_t time_valid{ 0 };
    std::string sgid;
    uint64_t gas_price{ 0 };
    int32_t step = pools::protobuf::kNormalFrom;
    std::string from_addr;
    std::string tx_hash;
};

typedef std::shared_ptr<TxItem> TxItemPtr;

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
        std::vector<TxItemPtr>& res_vec);
    void TxOver(std::vector<TxItemPtr>& txs);
    void TxRecover(std::vector<TxItemPtr>& txs);
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
