#pragma once

#include <chrono>
#include <mutex>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "block/block_utils.h"
#include "common/utils.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "common/unique_map.h"
#include "common/unique_set.h"
#include "db/db.h"
#include "protos/prefix_db.h"
#include "protos/sync.pb.h"
#include "protos/transport.pb.h"
#include "sync/sync_utils.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace dht {
    class BaseDht;
    typedef std::shared_ptr<BaseDht> BaseDhtPtr;
}  // namespace dht

namespace block {
    class BlockManager;
}

namespace consensus {
    class HotstuffManager;
}

namespace pools {
    class PoolManager;
}

namespace sync {

using ViewBlockSyncedCallback = std::function<int(const view_block::protobuf::ViewBlockItem& pb_vblock)>;

enum SyncItemTag : uint32_t {
    kBlockHeight = 1,
    kViewHash = 2,
    kBlockView = 3,
};

class SyncItem {
public:
    SyncItem(uint32_t net_id, const std::string& in_key, uint32_t pri)
            : network_id(net_id), key(in_key), 
            priority(pri), sync_times(0), responsed_timeout_us(common::kInvalidUint64) {
        tag = kViewHash;
        sync_tm_us = 0;
        common::GlobalInfo::Instance()->AddSharedObj(9);
    }

    SyncItem(uint32_t net_id, uint32_t in_pool_idx, uint64_t in_height, uint32_t pri, uint32_t sync_tag)
            : network_id(net_id), pool_idx(in_pool_idx), 
            height(in_height), priority(pri), sync_times(0), responsed_timeout_us(common::kInvalidUint64) {
        key = std::to_string(network_id) + "_" +
            std::to_string(pool_idx) + "_" +
            std::to_string(height) + "_" + 
            std::to_string(sync_tag);
        tag = sync_tag;
        sync_tm_us = 0;
        common::GlobalInfo::Instance()->AddSharedObj(9);
    }

    ~SyncItem() {
        common::GlobalInfo::Instance()->DecSharedObj(9);
    }

    uint32_t network_id{ 0 };
    std::string key;
    uint32_t priority{ 0 };
    uint32_t sync_times{ 0 };
    uint32_t pool_idx{ common::kInvalidUint32 };
    uint64_t height{ common::kInvalidUint64 };
    uint64_t sync_tm_us;
    uint64_t responsed_timeout_us;
    uint32_t tag;
};

typedef std::shared_ptr<SyncItem> SyncItemPtr;
typedef std::shared_ptr<view_block::protobuf::ViewBlockItem> ViewBlockPtr;

class KeyValueSync {
public:
    KeyValueSync();
    ~KeyValueSync();
    void AddSyncHeight(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority);
    void AddSyncView(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority);
    void AddSyncViewHash(
        uint32_t network_id, 
        uint32_t pool_idx,
        const std::string& view_hash, 
        uint32_t priority);
    void Init(
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<consensus::HotstuffManager>& hotstuff_mgr,
        std::shared_ptr<pools::TxPoolManager> tx_pool_mgr,
        const std::shared_ptr<db::Db>& db,
        ViewBlockSyncedCallback view_block_synced_callback);
    void HandleMessage(const transport::MessagePtr& msg);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);

    void AddBroadcastGlobalBlock(const ViewBlockPtr& pb_vblock) {
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        broadcast_global_blocks_queues_[thread_idx].push(pb_vblock);
    }

    void OnNewElectBlock(uint32_t sharding_id, uint64_t height) {
        if (height > elect_net_heights_map_[sharding_id]) {
            elect_net_heights_map_[sharding_id] = height;
        }

        if (sharding_id > max_sharding_id_) {
            max_sharding_id_ = sharding_id;
        }
    }

private:
    struct VerifyBlockItem {
        ViewBlockPtr pb_vblock;
        std::string key;
        uint32_t tag{ 0 };
        bool is_broadcast{ false };
        uint64_t msg_hash{ 0 };
        uint64_t enqueue_tm_ms{ 0 };
    };

    struct VerifyBlockResult {
        ViewBlockPtr pb_vblock;
        std::string key;
        uint32_t tag{ 0 };
        bool is_broadcast{ false };
        uint64_t msg_hash{ 0 };
        int verify_res{ -1 };
        uint64_t enqueue_tm_ms{ 0 };
        uint64_t verify_cost_ms{ 0 };
    };

    void CheckSyncTimeout();
    uint64_t SendSyncRequest(
        uint32_t network_id,
        const sync::protobuf::SyncMessage& sync_msg,
        const std::set<uint64_t>& sended_neigbors);
    void ProcessSyncValueRequest(const transport::MessagePtr& msg_ptr);
    void ProcessSyncValueResponse(const transport::MessagePtr& msg_ptr);
    void PopItems();
    void ConsensusTimerMessage();
    void HotstuffConsensusTimerMessage(const transport::MessagePtr& msg_ptr);
    uint32_t PopKvMessage();
    void KvConsumerLoop();
    void HandleKvMessage(const transport::MessagePtr& msg_ptr);
    void ResponseElectBlock(
        uint32_t network_id,
        const sync::protobuf::SyncHeightItem& sync_item,
        transport::protobuf::Header& msg,
        sync::protobuf::SyncValueResponse* res,
        uint32_t& add_size);
    void BroadcastGlobalBlock();
    void SyncAllLatestBlocks();
    void HandlerVerifiedBlock(const std::map<uint32_t, std::map<uint32_t, std::map<uint64_t, std::shared_ptr<view_block::protobuf::ViewBlockItem>>>>& res_map);
    void QueueFollowupBlockSync(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height);
    void VerifyConsumerLoop();
    void EnqueueVerifyBlock(
        const ViewBlockPtr& pb_vblock,
        const std::string& key,
        uint32_t tag,
        bool is_broadcast,
        uint64_t msg_hash);
    void DrainVerifiedBlocks();
    void ApplyVerifiedBlockResult(const VerifyBlockResult& result);
    void EnqueueVerifiedBlock(const ViewBlockPtr& pb_vblock);

    static const uint64_t kSyncPeriodUs = 300000lu;
    static const uint64_t kSyncSendIntervalUs = 50000lu;
    // [SYNC_OPT] Reduced from 3,000,000µs (3s) to 800,000µs (800ms).
    // This is the deduplication window: if a sync request hasn't been answered
    // within this time, it can be re-sent. 3s was far too long — a block sync
    // round-trip should complete in <200ms on a healthy network. 300ms gives
    // enough margin for network jitter while allowing ~3 retries per second.
    static const uint64_t kSyncTimeoutPeriodUs = 300000lu;
    static const uint32_t kEachTimerHandleCount = 64u;
    // [SYNC_OPT] Increased from 4096 to 8192: drain more ready-queue messages
    // per timer tick. With faster sync, more responses arrive per interval.
    static const uint32_t kMaxBatchDrainCount = 8192u;
    static const uint32_t kCacheSyncKeyValueCount = 1024000u;
    static const uint32_t kSyncCount = 5u;
    static const uint32_t kMaxSyncLatestNotRootCount = 1024u;
    static const uint32_t kFollowupSyncHeightCount = 32u;
    static const uint32_t kLatestSyncBlocksPerPool = 32u;
    // [SYNC_OPT] Increased from 1024 to 4096: consumer thread relays more
    // messages per wakeup to keep up with higher sync throughput.
    static const uint32_t kConsumerBatchSize = 4096u;
    static const uint32_t kVerifyThreadCount = 4u;
    static const uint32_t kMaxVerifiedDrainCount = 4096u;
    static const uint32_t kLatestSyncPeerFanout = 2u;

    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> tx_pool_mgr_ = nullptr;
    common::ThreadSafeQueue<SyncItemPtr> item_queues_[common::kMaxThreadCount];
    common::ThreadSafeQueue<ViewBlockPtr> broadcast_global_blocks_queues_[common::kMaxThreadCount];
    common::UniqueMap<std::string, SyncItemPtr, kCacheSyncKeyValueCount> synced_map_;
    std::map<uint32_t, std::map<uint32_t, std::map<uint64_t, std::pair<bool, std::shared_ptr<view_block::protobuf::ViewBlockItem>>>>> synced_res_map_;
    uint32_t not_root_synced_res_map_count_ = 0;
    common::Tick kv_tick_;
    std::queue<transport::MessagePtr> kv_msg_queue_;
    // Messages relayed by consumer thread, processed by timer thread.
    // Responses are prioritized so received blocks do not sit behind a large
    // batch of sync requests.
    common::ThreadSafeQueue<std::shared_ptr<transport::TransportMessage>> kv_ready_res_queue_;
    common::ThreadSafeQueue<std::shared_ptr<transport::TransportMessage>> kv_ready_req_queue_;
    uint64_t elect_net_heights_map_[network::kConsensusShardEndNetworkId] = { 0 };
    common::UniqueSet<std::string, kCacheSyncKeyValueCount> responsed_keys_;
    uint32_t max_sharding_id_ = network::kConsensusShardBeginNetworkId;
    ViewBlockSyncedCallback view_block_synced_callback_ = nullptr;
    common::ThreadSafeQueue<ViewBlockPtr> vblock_queues_[common::kMaxThreadCount];
    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_ = nullptr;
    std::mutex kv_msg_mutex_;
    std::condition_variable wait_con_;
    std::shared_ptr<std::thread> kv_consumer_thread_ = nullptr;
    std::queue<VerifyBlockItem> verify_block_queue_;
    std::queue<VerifyBlockResult> verified_block_queue_;
    std::unordered_set<std::string> verifying_keys_;
    std::mutex verify_mutex_;
    std::condition_variable verify_con_;
    std::vector<std::shared_ptr<std::thread>> verify_threads_;
    std::atomic<uint32_t> verifying_count_{0};
    std::atomic<bool> destroy_{false};
    uint64_t prev_sync_tm_ms_ = 0;
    uint64_t prev_sent_sync_tm_ms_ = 0;

    DISALLOW_COPY_AND_ASSIGN(KeyValueSync);
};

}  // namespace sync

}  // namespace shardora
