#pragma once

#include <bitset>
#include <memory>

#include "common/bitmap.h"
#include "common/thread_safe_queue.h"
#include "common/unique_map.h"
#include "pools/tx_pool.h"
#include "protos/address.pb.h"
#include "protos/pools.pb.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace pools {

class TxPoolManager {
public:
    TxPoolManager(std::shared_ptr<security::Security>& security);
    ~TxPoolManager();
    void GetTx(
        uint32_t pool_index,
        uint32_t count,
        std::map<std::string, TxItemPtr>& res_map);
    void GetTx(
        const common::BloomFilter& bloom_filter,
        uint32_t pool_index,
        std::map<std::string, TxItemPtr>& res_map);
    TxItemPtr GetTx(uint32_t pool_index, const std::string& sgid);
    void TxOver(uint32_t pool_index, std::map<std::string, TxItemPtr>& over_txs);
    void TxRecover(uint32_t pool_index, std::map<std::string, TxItemPtr>& recover_txs);
    void SetTimeout(uint32_t pool_index) {}
    uint64_t latest_height(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_height();
    }

    std::string latest_hash(uint32_t pool_index) const {
        return tx_pool_[pool_index].latest_hash();
    }

    // just for test
    int AddTx(uint32_t pool_index, TxItemPtr& tx_ptr) {
        if (pool_index >= common::kInvalidPoolIndex) {
            return kPoolsError;
        }

        return tx_pool_[pool_index].AddTx(tx_ptr);
    }

private:
    void HandleMessage(const transport::MessagePtr& msg);
    void SaveStorageToDb(const transport::protobuf::Header& msg);

    TxPool* tx_pool_{ nullptr };
    std::shared_ptr<security::Security> security_ = nullptr;
    common::ThreadSafeQueue<transport::MessagePtr> msg_queues_[common::kInvalidPoolIndex];

    DISALLOW_COPY_AND_ASSIGN(TxPoolManager);
};

}  // namespace pools

}  // namespace zjchain
