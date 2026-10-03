#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "block/block_utils.h"
#include "common/utils.h"
#include "network/network_utils.h"
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

// The three flows share one in-flight dedup map and one "already answered" set,
// so their keys must never collide.  A height probe, a height+view/hash identity
// probe and a view catch-up request all once produced keys of the form
// "net_pool_height_tag", which made a view request and a height request at the
// same number indistinguishable.  Every key for the height/view flows is now
// built through SyncKeyOf with the tag folded into a namespace prefix, so the
// flows can only collide if the tag matches too.
inline std::string SyncKeyPrefix(uint32_t tag) {
    switch (tag) {
    case kBlockHeight:
        return "h";
    case kBlockView:
        return "w";
    case kViewHash:
        return "s";
    default:
        return "u";
    }
}

// Key for the height flow and the view flow.  `identity` is the view (height
// flow, only when the exact branch is known) or the requested view (view flow);
// `block_hash` is the hex block hash for a height identity probe.  Both are
// appended only when meaningful, so a bare probe and an identity probe stay
// distinct entries and neither is mistaken for the other.
inline std::string SyncKeyOf(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t number,
        uint32_t tag,
        uint64_t identity = common::kInvalidUint64,
        const std::string& block_hash = std::string()) {
    std::string key = SyncKeyPrefix(tag) + "_" +
        std::to_string(network_id) + "_" +
        std::to_string(pool_idx) + "_" +
        std::to_string(number);
    if (identity != common::kInvalidUint64) {
        key += "_v" + std::to_string(identity);
    }

    if (!block_hash.empty()) {
        key += "_h" + common::Encode::HexEncode(block_hash);
    }

    return key;
}

class SyncItem {
public:
    SyncItem(uint32_t net_id, const std::string& in_key, uint32_t pri)
            : network_id(net_id), key(in_key),
            priority(pri), sync_times(0), responsed_timeout_us(common::kInvalidUint64) {
        tag = kViewHash;
        sync_tm_us = 0;
        common::GlobalInfo::Instance()->AddSharedObj(9);
    }

    SyncItem(
            uint32_t net_id,
            uint32_t in_pool_idx,
            uint64_t in_height,
            uint32_t pri,
            uint32_t sync_tag)
            : SyncItem(net_id, in_pool_idx, in_height, pri, sync_tag,
                common::kInvalidUint64, std::string(), false) {}

    SyncItem(
            uint32_t net_id,
            uint32_t in_pool_idx,
            uint64_t in_height,
            uint32_t pri,
            uint32_t sync_tag,
            bool in_single_view)
            : SyncItem(net_id, in_pool_idx, in_height, pri, sync_tag,
                common::kInvalidUint64, std::string(), in_single_view) {}

    // A height is not a unique identity: the same height can be produced by
    // several views when the chain forks, so two requests for "height H" may
    // legitimately want two different blocks.  When the caller knows which
    // branch it needs, in_view / in_block_hash narrow the key and the request
    // to that exact block; otherwise the item is a probe for "whatever height H
    // resolves to" and must not be treated as proof that H is already synced.
    SyncItem(
            uint32_t net_id,
            uint32_t in_pool_idx,
            uint64_t in_height,
            uint32_t pri,
            uint32_t sync_tag,
            uint64_t in_view,
            const std::string& in_block_hash,
            bool in_single_view = false)
            : network_id(net_id), pool_idx(in_pool_idx),
            height(in_height), view(in_view), block_hash(in_block_hash),
            priority(pri), sync_times(0), responsed_timeout_us(common::kInvalidUint64) {
        key = SyncKeyOf(network_id, pool_idx, height, sync_tag, view, block_hash);
        tag = sync_tag;
        single_view = in_single_view;
        sync_tm_us = 0;
        common::GlobalInfo::Instance()->AddSharedObj(9);
    }

    ~SyncItem() {
        common::GlobalInfo::Instance()->DecSharedObj(9);
    }

    // True when this item names a specific branch rather than merely a height.
    bool HasIdentity() const {
        return view != common::kInvalidUint64 || !block_hash.empty();
    }

    uint32_t network_id{ 0 };
    std::string key;
    uint32_t priority{ 0 };
    uint32_t sync_times{ 0 };
    uint32_t pool_idx{ common::kInvalidUint32 };
    uint64_t height{ common::kInvalidUint64 };
    uint64_t view{ common::kInvalidUint64 };
    std::string block_hash;
    uint64_t sync_tm_us;
    uint64_t responsed_timeout_us;
    uint32_t tag;
    // kBlockView only: true asks the responder for exactly `height` as a view
    // and nothing later; false asks for that view onward (a catch-up).
    bool single_view{ false };
};

// Canonical dedup key for a block that has been fully identified.  height is
// deliberately excluded: the same block can be reached via height-sync,
// view-hash-sync or broadcast, and all three must collapse onto one entry.
inline std::string SyncedBlockKey(
        uint32_t network_id,
        uint32_t pool_idx,
        const std::string& view_block_hash) {
    return std::to_string(network_id) + "_" +
        std::to_string(pool_idx) + "_" +
        common::Encode::HexEncode(view_block_hash);
}

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
    // Height sync for a known branch: in_view / in_block_hash identify the
    // exact block wanted at that height.  Either may be left unset to fall back
    // to a plain height probe.
    void AddSyncHeight(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority,
        uint64_t in_view,
        const std::string& in_block_hash);
    // Catch-up request: the peer answers with `view` onward, as many later
    // views as fit in one packet.
    void AddSyncView(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t view,
        uint32_t priority);
    // Exact-view request: the peer answers with `view` alone.  Used when the
    // node knows the one view it needs — a gap it is filling, or a view whose
    // own block still lacks a QC — so spilling later views would only crowd the
    // packet with branches it did not ask for.
    void AddSyncViewSingle(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t view,
        uint32_t priority);
    // True when the local chain already holds `view` with a valid QC.  Both
    // AddSyncView and AddSyncViewSingle drop such a request instead of queuing
    // it: a view with a valid QC is immutable and already local, so syncing it
    // again could only waste a round-trip and a packet slot.
    bool ViewAlreadySettled(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t view);
    void AddSyncViewHash(
        uint32_t network_id,
        uint32_t pool_idx,
        const std::string& view_hash,
        uint32_t priority);
    // Consensus rejected `view_block_hash` as an illegal fork of the chain it
    // holds at that height.  Forget the candidate and any "already answered"
    // marker for that height so the next round requests the height again, which
    // is how the valid branch gets pulled in.
    void DropSyncedCandidate(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        const std::string& view_block_hash);
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

        // A new elect block is exactly the dependency a suspended chain was
        // waiting on, so release every pool of this shard immediately instead
        // of leaving them parked until the timeout.
        {
            std::lock_guard<std::mutex> lock(verify_mutex_);
            for (auto it = suspended_chains_.begin(); it != suspended_chains_.end();) {
                if (it->first == sharding_id) {
                    ResumeChainLocked(*it);
                    suspended_tm_ms_.erase(*it);
                    it = suspended_chains_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        verify_con_.notify_all();
    }

private:
    struct VerifyBlockItem {
        ViewBlockPtr pb_vblock;
        std::string key;
        uint32_t tag{ 0 };
        bool is_broadcast{ false };
        uint64_t msg_hash{ 0 };
        uint64_t enqueue_tm_ms{ 0 };
        // Scheduling keys, cached at enqueue time so the queue comparator never
        // touches the protobuf.  Blocks are ordered by (shard, pool, height):
        // root congress first, then the other shards, then the local shard
        // last, and inside one (shard, pool) the lowest height always wins.
        uint32_t network_id{ 0 };
        uint32_t pool_idx{ 0 };
        uint64_t height{ 0 };
        uint32_t rank{ 0 };
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
        // Same scheduling keys as VerifyBlockItem so both queues share one
        // comparator.
        uint32_t network_id{ 0 };
        uint32_t pool_idx{ 0 };
        uint64_t height{ 0 };
        uint32_t rank{ 0 };
    };

    // Sort rank for the verify queue: root congress first, the local shard
    // last, everything else in between.
    static uint32_t VerifyRank(uint32_t network_id) {
        if (network_id == network::kRootCongressNetworkId) {
            return 0u;
        }

        if (network::IsSameToLocalShard(network_id)) {
            return 2u;
        }

        return 1u;
    }

    // std::priority_queue pops the "largest" element, so the comparator returns
    // true when `lhs` is LESS urgent than `rhs`: lower rank first, then lower
    // height inside the same (shard, pool), with enqueue time breaking ties so
    // equal-height siblings keep arrival order.  Both item types expose the same
    // scheduling members, so one template serves both queues.
    template <typename T>
    struct VerifyBlockLess {
        bool operator()(const T& lhs, const T& rhs) const {
            if (lhs.rank != rhs.rank) {
                return lhs.rank > rhs.rank;
            }

            if (lhs.network_id != rhs.network_id) {
                return lhs.network_id > rhs.network_id;
            }

            if (lhs.pool_idx != rhs.pool_idx) {
                return lhs.pool_idx > rhs.pool_idx;
            }

            if (lhs.height != rhs.height) {
                return lhs.height > rhs.height;
            }

            return lhs.enqueue_tm_ms > rhs.enqueue_tm_ms;
        }
    };

    // Identifies one (shard, pool) chain.
    using SyncChainKey = std::pair<uint32_t, uint32_t>;
    static SyncChainKey ChainKeyOf(const VerifyBlockItem& item) {
        return std::make_pair(item.network_id, item.pool_idx);
    }

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
    // Park `item`'s chain so later heights wait for the missing dependency.
    // Caller must hold verify_mutex_.
    void SuspendChainLocked(const SyncChainKey& chain, uint64_t now_tm_ms);
    // Move every parked item back into the ready queue.  Caller must hold
    // verify_mutex_.
    void ResumeChainLocked(const SyncChainKey& chain);
    // Resume chains that have waited longer than kVerifySuspendMaxWaitMs.
    // Caller must hold verify_mutex_.
    void ResumeExpiredChainsLocked(uint64_t now_tm_ms);
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
    // Upper bound on how many consecutive views one kBlockView response may
    // carry.  The packet-size check is the real limit; this only stops a large
    // view gap from building an unbounded vector before the size check runs.
    static const uint32_t kMaxViewPerResponse = 64u;
    // [SYNC_OPT] Increased from 1024 to 4096: consumer thread relays more
    // messages per wakeup to keep up with higher sync throughput.
    static const uint32_t kConsumerBatchSize = 4096u;
    static const uint32_t kVerifyThreadCount = 4u;
    static const uint32_t kMaxVerifiedDrainCount = 4096u;
    // A suspended (shard, pool) chain is retried after this long even if the
    // elect block it was waiting on never shows up, so one missing dependency
    // cannot freeze the chain forever.
    static const uint64_t kVerifySuspendMaxWaitMs = 300lu;
    static const uint32_t kLatestSyncPeerFanout = 2u;

    // One candidate block received for a given height.  Several may coexist for
    // the same height when the chain forks; only the one that actually gets
    // committed stays in the pool map once the height advances.
    struct SyncedHeightEntry {
        bool verified{ false };
        bool pushed_to_consensus{ false };
        // A candidate whose verification failed can never be used, but leaving
        // it in the map is worse than removing it: a height is only considered
        // "answered" while it holds a selectable candidate, so an unusable one
        // would block the height forever.  `dead` marks it for eviction and
        // makes it invisible to SelectHeightEntry.
        bool dead{ false };
        // Verification failures are not all permanent (a parent may simply not
        // have arrived yet), so a candidate is only retired after repeated
        // failures.  Without the cap a height could pin its head candidate
        // forever, because SelectHeightEntry always tries the same one first.
        uint32_t verify_fail_count{ 0 };
        uint64_t recv_tm_us{ 0 };
        uint64_t last_attempt_tm_us{ 0 };
        ViewBlockPtr pb_vblock;
    };

    // How many times one candidate may fail verification before it is retired
    // and the next candidate at that height gets its turn.
    static const uint32_t kMaxVerifyFailCount = 2u;
    // A candidate that has not been requested for this long is considered
    // abandoned and stops counting as "an answer for this height".
    static const uint64_t kHeightCandidateTtlUs = 30000000lu;

    // Per-(network, pool) pool of received blocks:
    //   height -> { block_hash -> entry }
    // The old shape was height -> single block, so a sibling view arriving for
    // the same height silently overwrote the first one and the height looked
    // "done" forever after.
    using SyncedHeightMap = std::map<uint64_t, std::map<std::string, SyncedHeightEntry>>;
    using SyncedPoolMap = std::map<uint32_t, SyncedHeightMap>;
    using SyncedNetworkMap = std::map<uint32_t, SyncedPoolMap>;

    // True while the height still holds a candidate that could be committed:
    // not dead and seen recently enough.  A height full of dead or expired
    // candidates has to become requestable again, both for an identity probe
    // and for a bare height probe.
    static bool HeightHasLiveCandidate(
            const SyncedHeightMap& height_map,
            uint64_t height,
            uint64_t now_tm_us);
    // Removes the candidates that lost the branch choice at a settled height.
    static void DropLosingCandidates(
            SyncedHeightMap* height_map,
            uint64_t height,
            const std::string& keep_hash,
            uint32_t* dropped_count);

    // Pick the entry at `height` that consensus should receive next.  Prefers
    // the branch already committed locally, otherwise the highest-view
    // candidate, so a fork converges instead of ping-ponging.  Candidates that
    // are dead (verification failed) or already handed to consensus are skipped
    // so an unusable head candidate cannot pin the height forever.
    static ViewBlockPtr SelectHeightEntry(
            const SyncedHeightMap& height_map,
            uint64_t height,
            uint32_t network_id,
            uint32_t pool_idx,
            const std::shared_ptr<consensus::HotstuffManager>& hotstuff_mgr,
            uint32_t max_retry);
    // Advance latest_height while consecutive heights have a candidate that has
    // been verified, enqueueing ready blocks to consensus.
    uint32_t DrainConsecutiveHeights(
            uint32_t network_id,
            uint32_t pool_idx,
            uint64_t latest_height,
            SyncedHeightMap* height_map,
            uint32_t max_drain);
    // Drop everything at or below `latest_height` and keep the counters honest.
    void EraseSyncedHeightsUpTo(
            uint32_t network_id,
            uint32_t pool_idx,
            uint64_t latest_height,
            SyncedHeightMap* height_map);

    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> tx_pool_mgr_ = nullptr;
    common::ThreadSafeQueue<SyncItemPtr> item_queues_[common::kMaxThreadCount];
    common::ThreadSafeQueue<ViewBlockPtr> broadcast_global_blocks_queues_[common::kMaxThreadCount];
    common::UniqueMap<std::string, SyncItemPtr, kCacheSyncKeyValueCount> synced_map_;
    SyncedNetworkMap synced_res_map_;
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
    // Verify work, ordered so a (shard, pool) chain is always verified from its
    // lowest pending height upwards instead of in arrival order.  `verify_con_`
    // and `verify_mutex_` guard both queues and the suspend state below.
    std::priority_queue<
        VerifyBlockItem, std::vector<VerifyBlockItem>, VerifyBlockLess<VerifyBlockItem>>
            verify_block_queue_;
    std::priority_queue<
        VerifyBlockResult, std::vector<VerifyBlockResult>, VerifyBlockLess<VerifyBlockResult>>
            verified_block_queue_;
    std::unordered_set<std::string> verifying_keys_;
    // A (shard, pool) chain whose verify returned "elect item not found" is
    // parked: every later height of that chain must wait for the missing block
    // rather than be verified ahead of it.  Items of a parked chain are held in
    // `suspended_items_` instead of the ready queue so they cannot occupy the
    // head of a priority queue until the chain is resumed.
    std::set<SyncChainKey> suspended_chains_;
    std::multimap<SyncChainKey, VerifyBlockItem> suspended_items_;
    // When the chain was first parked, used to force a resume after
    // kVerifySuspendMaxWaitMs in case the missing elect block never arrives.
    std::map<SyncChainKey, uint64_t> suspended_tm_ms_;
    std::mutex verify_mutex_;
    std::condition_variable verify_con_;
    std::vector<std::shared_ptr<std::thread>> verify_threads_;
    std::atomic<uint32_t> verifying_count_{0};
    std::atomic<bool> destroy_{false};
    std::atomic<bool> initialized_{false};
    uint64_t prev_sync_tm_ms_ = 0;
    uint64_t prev_sent_sync_tm_ms_ = 0;

    DISALLOW_COPY_AND_ASSIGN(KeyValueSync);
};

}  // namespace sync

}  // namespace shardora
