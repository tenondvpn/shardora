#pragma once

#include <chrono>
#include <mutex>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>

#include "block/block_utils.h"
#include "common/utils.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "db/db.h"
#include "protos/prefix_db.h"
#include "protos/sync.pb.h"
#include "protos/transport.pb.h"
#include "sync/sync_utils.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace dht {
    class BaseDht;
    typedef std::shared_ptr<BaseDht> BaseDhtPtr;
}  // namespace dht

namespace block {
    class BlockManager;
}

namespace sync {

struct SyncItem {
    SyncItem(uint32_t net_id, const std::string& in_key, uint32_t pri)
            : network_id(net_id), key(in_key), priority(pri), sync_times(0) {}

    SyncItem(uint32_t net_id, uint32_t in_pool_idx, uint64_t in_height, uint32_t pri)
            : network_id(net_id), pool_idx(in_pool_idx), height(in_height), priority(pri), sync_times(0) {
        key = std::to_string(network_id) + "_" +
            std::to_string(pool_idx) + "_" +
            std::to_string(height);
    }

    uint32_t network_id{ 0 };
    std::string key;
    uint32_t priority{ 0 };
    uint32_t sync_times{ 0 };
    uint32_t pool_idx{ common::kInvalidUint32 };
    uint64_t height{ common::kInvalidUint64 };
    uint64_t sync_tm_us;
};

typedef std::shared_ptr<SyncItem> SyncItemPtr;

class KeyValueSync {
public:
    KeyValueSync();
    ~KeyValueSync();
    void AddSync(
        uint8_t thread_idx,
        uint32_t network_id,
        const std::string& key,
        uint32_t priority);
    void AddSyncHeight(
        uint8_t thread_idx,
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority);
    void Init(
        block::BlockAggValidCallback block_agg_valid_func,
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<db::Db>& db);
    void HandleMessage(const transport::MessagePtr& msg);
    uint32_t added_key_size() const {
        return added_key_set_.size();
    }

private:
    void CheckSyncItem(uint8_t thread_idx);
    void CheckSyncTimeout();
    uint64_t SendSyncRequest(
        uint8_t thread_idx,
        uint32_t network_id,
        const sync::protobuf::SyncMessage& sync_msg,
        const std::set<uint64_t>& sended_neigbors);
    void ProcessSyncValueRequest(const transport::MessagePtr& msg_ptr);
    void ProcessSyncValueResponse(const transport::MessagePtr& msg_ptr);
    void PopItems();
    void ConsensusTimerMessage(uint8_t thread_idx);
    bool AddSyncKeyValue(transport::protobuf::Header* msg, const block::protobuf::Block& block);
    void PopKvMessage(uint8_t thread_idx);
    void HandleKvMessage(const transport::MessagePtr& msg_ptr);

    static const uint64_t kSyncPeriodUs = 300000lu;
    static const uint64_t kSyncTimeoutPeriodUs = 300000lu;

    common::ThreadSafeQueue<SyncItemPtr> item_queues_[common::kMaxThreadCount];
    std::unordered_map<std::string, SyncItemPtr> synced_map_;
    std::queue<SyncItemPtr> prio_sync_queue_[kSyncHighest + 1];
    std::unordered_set<std::string> added_key_set_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint64_t prev_sync_tm_us_ = 0;
    uint64_t prev_sync_tmout_us_ = 0;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    block::BlockAggValidCallback block_agg_valid_func_ = nullptr;
    common::Tick tick_;
    common::ThreadSafeQueue<std::shared_ptr<transport::TransportMessage>> kv_msg_queue_;

    DISALLOW_COPY_AND_ASSIGN(KeyValueSync);
};

}  // namespace sync

}  // namespace zjchain
