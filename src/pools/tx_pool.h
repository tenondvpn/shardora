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
#include "common/spin_mutex.h"
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
    void GetTx(std::map<std::string, TxItemPtr>& res_map, uint32_t count);

    inline TxItemPtr GetTx(const std::string& tx_hash) {
//         common::AutoSpinLock lock(mutex_);
        auto iter = added_tx_map_.find(tx_hash);
        if (iter != added_tx_map_.end()) {
            //         ZJC_DEBUG("success get tx %u, %s", pool_index_, common::Encode::HexEncode(tx_hash).c_str());
            return iter->second;
        }

        //     ZJC_DEBUG("failed get tx %u, %s", pool_index_, common::Encode::HexEncode(tx_hash).c_str());
        return nullptr;
    }

    std::shared_ptr<consensus::WaitingTxsItem> GetTx(const google::protobuf::RepeatedPtrField<std::string>& tx_hash_list) {
        auto txs_items = std::make_shared<consensus::WaitingTxsItem>();
        auto& tx_map = txs_items->txs;
        {
//             common::AutoSpinLock lock(mutex_);
            for (int32_t i = 0; i < tx_hash_list.size(); ++i) {
                auto& txhash = tx_hash_list[i];
                auto iter = added_tx_map_.find(txhash);
                if (iter != added_tx_map_.end()) {
                    //         ZJC_DEBUG("success get tx %u, %s", pool_index_, common::Encode::HexEncode(tx_hash).c_str());
                    tx_map[txhash] = iter->second;
                }
            }
        }
        //     ZJC_DEBUG("failed get tx %u, %s", pool_index_, common::Encode::HexEncode(tx_hash).c_str());
        return txs_items;
    }

    void GetTx(
        const common::BloomFilter& bloom_filter,
        std::map<std::string, TxItemPtr>& res_map);
    void TxOver(std::map<std::string, TxItemPtr>& txs);
    void TxOver(const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list);
    void TxRecover(std::map<std::string, TxItemPtr>& txs);
    uint64_t latest_height() const {
        return latest_height_;
    }

    std::string latest_hash() const {
        return latest_hash_;
    }

    void UpdateLatestInfo(uint64_t height, const std::string& hash) {
        if (latest_height_ < height) {
            latest_height_ = height;
            latest_hash_ = hash;
        }
    }

private:
    bool CheckTimeoutTx(TxItemPtr& tx_ptr, uint64_t timestamp_now);
//     common::SpinMutex mutex_;
    std::deque<TxItemPtr> timeout_txs_;
    std::unordered_map<std::string, TxItemPtr> added_tx_map_;
    std::unordered_map<std::string, TxItemPtr> gid_map_;
    std::map<std::string, TxItemPtr> prio_map_;
    uint32_t pool_index_ = 0;
    uint64_t latest_height_ = 0;
    std::string latest_hash_;

    DISALLOW_COPY_AND_ASSIGN(TxPool);
};

}  // namespace pools

}  // namespace zjchain
