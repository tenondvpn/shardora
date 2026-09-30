#include "sync/key_value_sync.h"

#include <algorithm>

#include "block/block_manager.h"
#include "broadcast/broadcast_utils.h"
#include "common/defer.h"
#include "common/global_info.h"
#include "common/log.h"
#include "db/db.h"
#include "dht/base_dht.h"
#include "dht/dht_function.h"
#include "dht/dht_key.h"
#include "consensus/hotstuff/hotstuff_manager.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "network/universal_manager.h"
#include "protos/block.pb.h"
#include "protos/view_block.pb.h"
#include "pools/tx_pool_manager.h"
#include "sync/sync_utils.h"
#include "transport/processor.h"

namespace shardora {

namespace sync {

KeyValueSync::KeyValueSync() {}

KeyValueSync::~KeyValueSync() {
    destroy_ = true;
    // Unregister before joining so in-flight dispatches see destroy_=true
    network::Route::Instance()->UnRegisterMessage(common::kSyncMessage);
    wait_con_.notify_all();
    verify_con_.notify_all();
    if (kv_consumer_thread_ && kv_consumer_thread_->joinable()) {
        kv_consumer_thread_->join();
    }

    for (auto& thread : verify_threads_) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }
}

// ODR definitions: in-class `static const` scalars are not implicitly `inline`
// before C++17; gtest (EXPECT_GT, etc.) passes them by const-ref and odr-uses
// them, so the linker needs one definition per symbol.
const uint64_t KeyValueSync::kSyncPeriodUs;
const uint64_t KeyValueSync::kSyncSendIntervalUs;
const uint64_t KeyValueSync::kSyncTimeoutPeriodUs;
const uint32_t KeyValueSync::kEachTimerHandleCount;
const uint32_t KeyValueSync::kMaxBatchDrainCount;
const uint32_t KeyValueSync::kCacheSyncKeyValueCount;
const uint32_t KeyValueSync::kSyncCount;
const uint32_t KeyValueSync::kMaxSyncLatestNotRootCount;
const uint32_t KeyValueSync::kFollowupSyncHeightCount;
const uint32_t KeyValueSync::kLatestSyncBlocksPerPool;
const uint32_t KeyValueSync::kMaxViewPerResponse;
const uint32_t KeyValueSync::kConsumerBatchSize;
const uint32_t KeyValueSync::kVerifyThreadCount;
const uint32_t KeyValueSync::kMaxVerifiedDrainCount;
const uint32_t KeyValueSync::kLatestSyncPeerFanout;

void KeyValueSync::Init(
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<consensus::HotstuffManager>& hotstuff_mgr,
        std::shared_ptr<pools::TxPoolManager> tx_pool_mgr,
        const std::shared_ptr<db::Db>& db,
        ViewBlockSyncedCallback view_block_synced_callback) {
    if (initialized_.exchange(true)) {
        SHARDORA_WARN("KeyValueSync::Init called more than once, skipping");
        return;
    }
    SHARDORA_DEBUG("init key value sync 0");
    hotstuff_mgr_ = hotstuff_mgr;
    SHARDORA_DEBUG("init key value sync 1");
    view_block_synced_callback_ = view_block_synced_callback;
    tx_pool_mgr_ = tx_pool_mgr;
    SHARDORA_DEBUG("init key value sync 2");
    network::Route::Instance()->RegisterMessage(
        common::kSyncMessage,
        std::bind(&KeyValueSync::HandleMessage, this, std::placeholders::_1));
    SHARDORA_DEBUG("init key value sync 3");
    kv_tick_.CutOff(
        1000lu,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
    SHARDORA_DEBUG("init key value sync 4");
    transport::Processor::Instance()->RegisterProcessor(
        common::kHotstuffSyncTimerMessage,
        std::bind(&KeyValueSync::HotstuffConsensusTimerMessage, this, std::placeholders::_1));    
    SHARDORA_DEBUG("init key value sync 5");
    // Start dedicated consumer thread for kv_msg_queue_ to avoid backlog
    kv_consumer_thread_ = std::make_shared<std::thread>(&KeyValueSync::KvConsumerLoop, this);
    for (uint32_t i = 0; i < kVerifyThreadCount; ++i) {
        verify_threads_.push_back(std::make_shared<std::thread>(
            &KeyValueSync::VerifyConsumerLoop, this));
    }
    SHARDORA_DEBUG("init key value sync 6: consumer and verify threads started");
}

int KeyValueSync::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void KeyValueSync::AddSyncHeight(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority) {
    // return;
    //assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(network_id, pool_idx, height, priority, kBlockHeight);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    item_queues_[thread_idx].push(item);
    SHARDORA_DEBUG("block height add new sync item key: %s, priority: %u, %u_%u_%lu",
        item->key.c_str(), item->priority, network_id, pool_idx, height);
}

void KeyValueSync::AddSyncHeight(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority,
        uint64_t in_view,
        const std::string& in_block_hash) {
    auto item = std::make_shared<SyncItem>(
        network_id, pool_idx, height, priority, kBlockHeight, in_view, in_block_hash);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    item_queues_[thread_idx].push(item);
    SHARDORA_DEBUG("block height add new sync item key: %s, priority: %u, %u_%u_%lu, view: %lu",
        item->key.c_str(), item->priority, network_id, pool_idx, height, in_view);
}

void KeyValueSync::AddSyncView(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority) {
    // return;
    //assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(network_id, pool_idx, height, priority, kBlockView);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    item_queues_[thread_idx].push(item);
    SHARDORA_DEBUG("block height add new sync item key: %s, priority: %u, %u_%u_%lu",
        item->key.c_str(), item->priority, network_id, pool_idx, height);
}

// Decides which of the candidates collected at one height should be handed to
// consensus.  Consensus itself is the arbiter of forks, so the rule here is
// only about ordering the attempts:
//   1. the branch already committed locally (a later sibling that lost the
//      fork is dropped rather than re-verified forever),
//   2. otherwise the highest view — deterministic across nodes, so all peers
//      agree on which candidate is tried first,
//   3. then the hash itself, purely to make the ordering total.
ViewBlockPtr KeyValueSync::SelectHeightEntry(
        const SyncedHeightMap& height_map,
        uint64_t height,
        uint32_t network_id,
        uint32_t pool_idx,
        const std::shared_ptr<consensus::HotstuffManager>& hotstuff_mgr,
        uint32_t max_retry) {
    auto height_iter = height_map.find(height);
    if (height_iter == height_map.end() || height_iter->second.empty()) {
        return nullptr;
    }

    auto selectable = [](const SyncedHeightEntry& entry) {
        return entry.pb_vblock && !entry.dead;
    };

    std::string local_committed_hash;
    if (hotstuff_mgr) {
        auto chain = hotstuff_mgr->chain(pool_idx);
        if (chain) {
            auto committed = chain->LatestCommittedBlock();
            if (committed && committed->has_block_info() &&
                    committed->block_info().height() == height &&
                    committed->qc().network_id() == network_id) {
                local_committed_hash = committed->qc().view_block_hash();
            }
        }
    }

    if (!local_committed_hash.empty()) {
        auto iter = height_iter->second.find(local_committed_hash);
        if (iter != height_iter->second.end() && selectable(iter->second)) {
            return iter->second.pb_vblock;
        }
    }

    const SyncedHeightEntry* best = nullptr;
    for (auto& kv : height_iter->second) {
        const auto& entry = kv.second;
        if (!selectable(entry)) {
            continue;
        }

        // A candidate that already failed too often gets out of the way so the
        // competing branch can be tried.  This is what keeps one bad fork from
        // pinning the height: without it the highest-view candidate is picked
        // again on every pass and the run never advances.
        if (entry.verify_fail_count >= max_retry) {
            continue;
        }

        if (best == nullptr) {
            best = &entry;
            continue;
        }

        // Prefer a candidate that has not been handed to consensus yet.  Once a
        // block is pushed, a successor can only follow on the same branch, so
        // re-picking an already-pushed one would stall the height.
        if (best->pushed_to_consensus != entry.pushed_to_consensus) {
            if (best->pushed_to_consensus) {
                best = &entry;
            }

            continue;
        }

        auto best_view = best->pb_vblock->qc().view();
        auto cur_view = entry.pb_vblock->qc().view();
        if (cur_view > best_view) {
            best = &entry;
        } else if (cur_view == best_view &&
                entry.pb_vblock->qc().view_block_hash() >
                best->pb_vblock->qc().view_block_hash()) {
            best = &entry;
        }
    }

    return best != nullptr ? best->pb_vblock : nullptr;
}

bool KeyValueSync::HeightHasLiveCandidate(
        const SyncedHeightMap& height_map,
        uint64_t height,
        uint64_t now_tm_us) {
    auto height_iter = height_map.find(height);
    if (height_iter == height_map.end()) {
        return false;
    }

    for (auto& kv : height_iter->second) {
        const auto& entry = kv.second;
        if (!entry.pb_vblock || entry.dead) {
            continue;
        }

        // An entry that has neither been verified nor touched for a full TTL is
        // treated as abandoned: the peer that was going to answer is gone, and
        // remembering it would keep the height permanently "answered" without
        // ever producing a usable block.
        if (!entry.verified && entry.recv_tm_us != 0 &&
                entry.recv_tm_us + kHeightCandidateTtlUs <= now_tm_us) {
            continue;
        }

        return true;
    }

    return false;
}

// Once a height is settled only the chosen branch matters: a successor can only
// extend it, and keeping the sibling would let it be re-picked on every pass.
void KeyValueSync::DropLosingCandidates(
        SyncedHeightMap* height_map,
        uint64_t height,
        const std::string& keep_hash,
        uint32_t* dropped_count) {
    if (height_map == nullptr) {
        return;
    }

    auto height_iter = height_map->find(height);
    if (height_iter == height_map->end()) {
        return;
    }

    auto& hash_map = height_iter->second;
    for (auto iter = hash_map.begin(); iter != hash_map.end(); ) {
        if (iter->first == keep_hash) {
            ++iter;
            continue;
        }

        if (dropped_count != nullptr) {
            ++(*dropped_count);
        }

        iter = hash_map.erase(iter);
    }
}

// Walks up from latest_height + 1 and pushes every consecutive height that has
// a verified candidate to consensus.  Returns the number of heights drained.
// The branch choice per height is SelectHeightEntry's job; this function only
// decides how far the run of consecutive heights reaches.
uint32_t KeyValueSync::DrainConsecutiveHeights(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t latest_height,
        SyncedHeightMap* height_map,
        uint32_t max_drain) {
    if (height_map == nullptr || latest_height == common::kInvalidUint64) {
        return 0;
    }

    uint32_t drained = 0;
    auto now_tm_us = common::TimeUtils::TimestampUs();
    auto next_height = latest_height + 1;
    auto height_iter = height_map->find(next_height);
    while (height_iter != height_map->end() && drained < max_drain) {
        auto pb_vblock = SelectHeightEntry(
            *height_map,
            next_height,
            network_id,
            pool_idx,
            hotstuff_mgr_,
            kMaxVerifyFailCount);
        if (!pb_vblock) {
            // Every candidate at this height is dead or retired, so the height
            // can never advance.  Report it as missing instead of stopping the
            // run silently, so the caller re-requests it.
            break;
        }

        const auto& selected_hash = pb_vblock->qc().view_block_hash();
        auto entry_iter = height_iter->second.find(selected_hash);
        if (entry_iter == height_iter->second.end()) {
            break;
        }

        auto& entry = entry_iter->second;
        if (!entry.verified) {
            // Ask a worker to verify the chosen branch, then stop the run: a
            // later height cannot be committed ahead of this one.  The retry is
            // throttled so a slow worker is not flooded, and the attempt is
            // counted so a candidate that never verifies eventually makes way
            // for its sibling instead of pinning the height.
            if (entry.last_attempt_tm_us == 0 ||
                    entry.last_attempt_tm_us + kSyncTimeoutPeriodUs <= now_tm_us) {
                entry.last_attempt_tm_us = now_tm_us;
                entry.recv_tm_us = now_tm_us;
                std::string key = SyncedBlockKey(network_id, pool_idx, selected_hash);
                EnqueueVerifyBlock(pb_vblock, key, kBlockHeight, false, 0);
            }

            break;
        }

        if (!entry.pushed_to_consensus) {
            entry.pushed_to_consensus = true;
            entry.recv_tm_us = now_tm_us;
            EnqueueVerifiedBlock(pb_vblock);
            ++drained;
        }

        // The chosen branch won at this height, so its siblings cannot be used
        // any more: a successor extends the winner only.  Dropping them here is
        // what makes a fork converge instead of being re-synced forever.
        uint32_t dropped = 0;
        DropLosingCandidates(height_map, next_height, selected_hash, &dropped);
        if (dropped > 0 && network_id != network::kRootCongressNetworkId) {
            if (dropped > not_root_synced_res_map_count_) {
                not_root_synced_res_map_count_ = 0;
            } else {
                not_root_synced_res_map_count_ -= dropped;
            }
        }

        ++next_height;
        height_iter = height_map->find(next_height);
    }

    return drained;
}

// Drops every candidate at or below latest_height.  Those heights are settled
// (either committed or abandoned), so keeping sibling branches around only
// leaks memory and skews not_root_synced_res_map_count_.
void KeyValueSync::EraseSyncedHeightsUpTo(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t latest_height,
        SyncedHeightMap* height_map) {
    if (height_map == nullptr || latest_height == common::kInvalidUint64) {
        return;
    }

    auto erase_end = height_map->upper_bound(latest_height);
    if (erase_end == height_map->begin()) {
        return;
    }

    if (network_id != network::kRootCongressNetworkId) {
        uint32_t erased_entries = 0;
        for (auto iter = height_map->begin(); iter != erase_end; ++iter) {
            erased_entries += static_cast<uint32_t>(iter->second.size());
        }
        if (erased_entries > not_root_synced_res_map_count_) {
            not_root_synced_res_map_count_ = 0;
        } else {
            not_root_synced_res_map_count_ -= erased_entries;
        }
    }

    height_map->erase(height_map->begin(), erase_end);
}

void KeyValueSync::HotstuffConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    std::shared_ptr<view_block::protobuf::ViewBlockItem> pb_vblock = nullptr;
    // SHARDORA_DEBUG("now call ConsensusTimerMessage thread_idx: %d", thread_idx);
    uint32_t handled = 0;
    static const uint32_t kMaxSyncBlocksPerTick = 32u;
    while (handled < kMaxSyncBlocksPerTick && vblock_queues_[thread_idx].pop(&pb_vblock)) {
        if (pb_vblock) {
            SHARDORA_DEBUG("hotstuff consensus timer message handle view block: %u_%u_%lu_%lu, timeblock_height: %lu",
                pb_vblock->qc().network_id(), 
                pb_vblock->qc().pool_index(), 
                pb_vblock->block_info().height(),
                pb_vblock->qc().view(), 
                pb_vblock->block_info().timeblock_height());
            if (!network::IsSameShardOrSameWaitingPool(
                    network::kRootCongressNetworkId, 
                    pb_vblock->qc().network_id()) && 
                    !network::IsSameToLocalShard(pb_vblock->qc().network_id())) {
                hotstuff_mgr_->hotstuff(pb_vblock->qc().network_id())->HandleSyncedViewBlock(
                    pb_vblock);
            } else {
                hotstuff_mgr_->hotstuff(pb_vblock->qc().pool_index())->HandleSyncedViewBlock(
                    pb_vblock);
            }
            ++handled;
        }
    }

    if (handled > 0) {
        SHARDORA_DEBUG("HotstuffConsensusTimerMessage handled synced blocks: %u, "
            "thread_idx: %u, remaining queue: %lu",
            handled,
            thread_idx,
            vblock_queues_[thread_idx].size());
    }

    BroadcastGlobalBlock();
}

void KeyValueSync::BroadcastGlobalBlock() {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    std::shared_ptr<view_block::protobuf::ViewBlockItem> view_block_ptr = nullptr;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    protobuf::SyncMessage& res_sync_msg = *msg.mutable_sync_proto();
    auto sync_res = res_sync_msg.mutable_sync_value_res();
    uint32_t add_size = 0;
    while (broadcast_global_blocks_queues_[thread_idx].pop(&view_block_ptr)) {
        if (view_block_ptr) {
            auto res = sync_res->add_res();
            res->set_network_id(view_block_ptr->qc().network_id());
            res->set_pool_idx(view_block_ptr->qc().pool_index());
            res->set_height(view_block_ptr->block_info().height());
            res->set_value(SerializeDeterministic(*view_block_ptr));
            res->set_key("");
            res->set_tag(kBlockHeight);
            add_size += 16 + res->value().size();
            SHARDORA_DEBUG("handle sync value view add add_size: %u  "
                "net: %u, pool: %u, height: %lu",
                add_size,
                res->network_id(),
                res->pool_idx(),
                res->height());
            if (add_size >= kSyncPacketMaxSize) {
                SHARDORA_DEBUG("handle sync value view add_size failed "
                    "net: %u, pool: %u, height: %lu",
                    res->network_id(),
                    res->pool_idx(),
                    res->height());
                break;
            }
        }
    }

    if (add_size == 0) {
        return;
    }

    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(network::kNodeNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kSyncMessage);
    auto* broadcast = msg.mutable_broadcast();
    broadcast::SetDefaultBroadcastParam(broadcast);
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    network::Route::Instance()->Send(msg_ptr);
    SHARDORA_DEBUG("sync global block ok des: %u, des hash64: %lu,",
        network::kNodeNetworkId, msg.hash64());
}

void KeyValueSync::AddSyncViewHash(
        uint32_t network_id, 
        uint32_t pool_idx,
        const std::string& view_hash, 
        uint32_t priority) {
    // return;
    //assert(!view_hash.empty());
    std::string key(2 + view_hash.size(), '\0');
    uint16_t* pools = reinterpret_cast<uint16_t*>(&key[0]);
    pools[0] = pool_idx;
    memcpy(&key[2], view_hash.c_str(), view_hash.size());
    //assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(
        network_id, key, priority);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    item_queues_[thread_idx].push(item);
    SHARDORA_DEBUG("block height add new sync item key: %s, %u_%u, priority: %u, item size: %u",
        common::Encode::HexEncode(item->key).c_str(), 
        network_id,
        pool_idx,
        item->priority, 
        item_queues_[thread_idx].size());
}

void KeyValueSync::ConsensusTimerMessage() {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    DrainVerifiedBlocks();
    // Drain messages relayed by the consumer thread. Responses are drained
    // first so block data is not delayed by request backlog. All processing
    // still runs here on the single timer thread to avoid shared-state races.
    {
        uint32_t processed = 0;
        transport::MessagePtr msg_ptr = nullptr;
        while (processed < kMaxBatchDrainCount) {
            msg_ptr = nullptr;
            if (!kv_ready_res_queue_.pop(&msg_ptr) || msg_ptr == nullptr) {
                break;
            }
            HandleKvMessage(msg_ptr);
            ++processed;
        }
        while (processed < kMaxBatchDrainCount) {
            msg_ptr = nullptr;
            if (!kv_ready_req_queue_.pop(&msg_ptr) || msg_ptr == nullptr) {
                break;
            }
            HandleKvMessage(msg_ptr);
            ++processed;
        }
    }
    auto now_tm_ms1 = common::TimeUtils::TimestampMs();
    DrainVerifiedBlocks();
    PopItems();
    auto now_tm_ms2 = common::TimeUtils::TimestampMs();
    // Note: Do NOT call GetViewBlockWithHash("", true) here.
    // The drain (pop from cached_block_queue_ + update cached_block_map_/LRU maps)
    // operates on non-thread-safe data structures that are owned by the consensus
    // thread. Draining from the sync timer thread causes data races and crashes.
    // The queue is drained naturally when consensus calls GetViewBlockWithHeight
    // or GetViewBlockWithView.

    auto now_tm_ms3 = common::TimeUtils::TimestampMs();
    auto etime = common::TimeUtils::TimestampMs();
    if (etime - now_tm_ms >= 1000000lu) {
        SHARDORA_ERROR("KeyValueSync handle message use time: %lu, "
            "PopKvMessage: %lu, PopItems: %lu, CheckSyncItem: %lu", 
            (etime - now_tm_ms), 
            (now_tm_ms1 - now_tm_ms),
            (now_tm_ms2 - now_tm_ms1),
            (now_tm_ms3 - now_tm_ms2));
        // //assert(false);
    }

    if (prev_sync_tm_ms_ + 500lu < now_tm_ms3) {
        SHARDORA_DEBUG("SyncAllLatestBlocks triggered, prev_sync_tm_ms: %lu, now: %lu",
            prev_sync_tm_ms_, now_tm_ms3);
        SyncAllLatestBlocks();
        prev_sync_tm_ms_ = now_tm_ms3;
    }

    // Adaptive timer: when there's a backlog of sync items or ready messages,
    // poll much faster (50µs) to drain them quickly. Otherwise use 1ms.
    uint64_t next_interval = 1000lu;
    uint32_t verify_pending = 0;
    uint32_t verified_pending = 0;
    {
        std::lock_guard<std::mutex> lock(verify_mutex_);
        verify_pending = static_cast<uint32_t>(verify_block_queue_.size());
        verified_pending = static_cast<uint32_t>(verified_block_queue_.size());
    }
    const uint32_t ready_res_size = static_cast<uint32_t>(kv_ready_res_queue_.size());
    const uint32_t ready_req_size = static_cast<uint32_t>(kv_ready_req_queue_.size());
    const uint32_t ready_size = ready_res_size + ready_req_size;
    if (ready_size > 32 || verify_pending > 0 || verified_pending > 0) {
        next_interval = 50lu;
    } else if (ready_size > 0) {
        next_interval = 200lu;
    }
    if (ready_size > 0 || verify_pending > 0 || verified_pending > 0) {
        SHARDORA_DEBUG("kv sync backlog ready_res: %u, ready_req: %u, verify_pending: %u, "
            "verified_pending: %u, next interval: %lu",
            ready_res_size,
            ready_req_size,
            verify_pending,
            verified_pending,
            next_interval);
    }
    kv_tick_.CutOff(
        next_interval,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
    // return count;
}

void KeyValueSync::PopItems() {
    std::set<uint64_t> sended_neigbors;
    std::map<uint32_t, sync::protobuf::SyncMessage> sync_dht_map;
    bool stop = false;
    auto now_tm = common::TimeUtils::TimestampUs();
    if (prev_sent_sync_tm_ms_ + kSyncSendIntervalUs > now_tm) {
        return;
    }

    prev_sent_sync_tm_ms_ = now_tm;
    uint32_t synced_count = 0;
    for (uint8_t thread_idx = 0; thread_idx < common::kMaxThreadCount; ++thread_idx) {
        while (true) {
            SyncItemPtr item = nullptr;
            item_queues_[thread_idx].pop(&item);
            if (item == nullptr) {
                break;
            }
            
            // Dedup on what the item actually identifies.
            //   - With a known hash: skip once that exact block has been
            //     received, because re-asking for it can only return the same
            //     block.
            //   - With a known view: skip once any candidate at that height
            //     carries that view, which is the same block by construction.
            //   - With neither: this is a probe for "whatever height H is".
            //     Any candidate at H means a peer answered this height, so
            //     stop probing — but a *different* branch can never satisfy
            //     it, which is why the branch-aware forms above exist.
            if (item->tag == kBlockHeight) {
                auto net_iter = synced_res_map_.find(item->network_id);
                if (net_iter != synced_res_map_.end()) {
                    auto pool_iter = net_iter->second.find(item->pool_idx);
                    if (pool_iter != net_iter->second.end()) {
                        auto height_iter = pool_iter->second.find(item->height);
                        if (height_iter != pool_iter->second.end()) {
                            if (item->HasIdentity()) {
                                bool matched = false;
                                for (auto& entry_iter : height_iter->second) {
                                    auto& entry = entry_iter.second;
                                    if (!entry.pb_vblock || entry.dead) {
                                        continue;
                                    }

                                    if (!item->block_hash.empty() &&
                                            entry.pb_vblock->qc().view_block_hash() != item->block_hash) {
                                        continue;
                                    }

                                    if (item->view != common::kInvalidUint64 &&
                                            entry.pb_vblock->qc().view() != item->view) {
                                        continue;
                                    }

                                    matched = true;
                                    break;
                                }

                                if (matched) {
                                    continue;
                                }
                            } else if (HeightHasLiveCandidate(
                                    pool_iter->second, item->height, now_tm)) {
                                // A bare height probe is satisfied by any live
                                // answer.  Candidates that died or expired do
                                // not count, otherwise a height that only ever
                                // received an unusable branch would look done
                                // and never be re-requested.
                                continue;
                            }
                        }
                    }
                }
            }

            if (synced_map_.get(item->key, &item)) {
                if (item->sync_tm_us + kSyncTimeoutPeriodUs >= now_tm) {
                    SHARDORA_DEBUG("item->sync_tm_us + kSyncTimeoutPeriodUs >= now_tm: %s", item->key.c_str());
                    continue;
                }

                // if (item->sync_times >= kSyncCount) {
                //     SHARDORA_DEBUG("item->sync_times >= kSyncCount: %s", item->key.c_str());
                //     continue;
                // }
            }

            if (responsed_keys_.exists(item->key)) {
                SHARDORA_DEBUG("responsed_keys_.exists(item->key): %s", item->key.c_str());
                continue;
            }

            auto iter = sync_dht_map.find(item->network_id);
            if (iter == sync_dht_map.end()) {
                sync_dht_map[item->network_id] = sync::protobuf::SyncMessage();
            }

            auto* sync_req = sync_dht_map[item->network_id].mutable_sync_value_req();
            sync_req->set_network_id(item->network_id);
            if (item->height != common::kInvalidUint64) {
                auto height_item = sync_req->add_heights();
                height_item->set_pool_idx(item->pool_idx);
                height_item->set_height(item->height);
                height_item->set_tag(item->tag);
                if (item->view != common::kInvalidUint64) {
                    height_item->set_view(item->view);
                }

                if (!item->block_hash.empty()) {
                    height_item->set_view_block_hash(item->block_hash);
                }

                SHARDORA_DEBUG("try to sync normal block: %u_%u_%lu, tag: %d, view: %lu",
                    item->network_id, item->pool_idx, item->height, item->tag, item->view);
            } else {
                sync_req->add_keys(item->key);
                SHARDORA_DEBUG("success add to sync key: %s", 
                    common::Encode::HexEncode(item->key).c_str());
            }

            if (sync_req->keys_size() + sync_req->heights_size() >
                    (int32_t)kEachRequestMaxSyncKeyCount) {
                uint64_t choose_node = SendSyncRequest(
                    item->network_id,
                    sync_dht_map[item->network_id],
                    sended_neigbors);
                if (choose_node != 0) {
                    sended_neigbors.insert(choose_node);
                }

                sync_req->clear_keys();
                sync_req->clear_heights();
            }

            ++(item->sync_times);
            synced_map_.add(item->key, item);
            item->sync_tm_us = now_tm;
            if (++synced_count > kSyncMaxKeyCount) {
                stop = true;
                break;
            }

            if (sended_neigbors.size() > kSyncNeighborCount) {
                stop = true;
                break;
            }     
        }

        if (stop) {
            break;
        }
    }

    for (auto iter = sync_dht_map.begin(); iter != sync_dht_map.end(); ++iter) {
        if (iter->second.sync_value_req().keys_size() > 0 ||
                iter->second.sync_value_req().heights_size() > 0) {
            uint64_t choose_node = SendSyncRequest(
                iter->first,
                iter->second,
                sended_neigbors);
            if (choose_node != 0) {
                sended_neigbors.insert(choose_node);
            }
        }
    }

    if (synced_count > 0) {
    }
}

uint64_t KeyValueSync::SendSyncRequest(
        uint32_t network_id,
        const sync::protobuf::SyncMessage& sync_msg,
        const std::set<uint64_t>& sended_neigbors) {
    std::vector<dht::NodePtr> nodes;
    SHARDORA_DEBUG("now get universal dht: %u", network_id);
    auto dht_ptr = network::UniversalManager::Instance()->GetUniversal(network::kUniversalNetworkId);
    auto dht = *dht_ptr->readonly_hash_sort_dht();
    dht::DhtFunction::GetNetworkNodes(dht, network_id, nodes);
    if (network_id >= network::kConsensusShardBeginNetworkId &&
            network_id <= network::kConsensusShardEndNetworkId) {
        dht::DhtFunction::GetNetworkNodes(dht, network_id + network::kConsensusWaitingShardOffset, nodes);
    } else if (network_id >= network::kConsensusWaitingShardBeginNetworkId &&
            network_id <= network::kConsensusWaitingShardEndNetworkId) {
        dht::DhtFunction::GetNetworkNodes(dht, network_id - network::kConsensusWaitingShardOffset, nodes);
    }

    if (nodes.empty()) {
        for (uint32_t i = network::kRootCongressNetworkId; i <= max_sharding_id_; ++i) {
            dht::DhtFunction::GetNetworkNodes(dht, i, nodes);
            if (!nodes.empty()) {
                break;
            }
        }

        if (nodes.empty()) {
            SHARDORA_ERROR("network id[%d] not exists.", network_id);
            return 0;
        }
    }

    uint32_t rand_pos = std::rand() % nodes.size();
    uint32_t choose_pos = rand_pos - 1;
    if (rand_pos == 0) {
        choose_pos = nodes.size() - 1;
    }

    dht::NodePtr node = nullptr;
    while (rand_pos != choose_pos) {
        auto iter = sended_neigbors.find(nodes[rand_pos]->id_hash);
        if (iter != sended_neigbors.end()) {
            ++rand_pos;
            if (rand_pos >= nodes.size()) {
                rand_pos = 0;
            }

            continue;
        }

        node = nodes[rand_pos];
        break;
    }

    if (!node) {
        node = nodes[rand() % nodes.size()];
    }

    transport::protobuf::Header msg;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(network_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kSyncMessage);
    *msg.mutable_sync_proto() = sync_msg;
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    transport::TcpTransport::Instance()->Send(node->public_ip, node->public_port, msg);
    SHARDORA_DEBUG("sync new from %s:%d, hash64: %lu, key size: %u, height size: %u, sync_msg: %s",
        node->public_ip.c_str(), node->public_port, msg.hash64(),
        sync_msg.sync_value_req().keys_size(),
        sync_msg.sync_value_req().heights_size(),
        ProtobufToJson(sync_msg).c_str());
    return node->id_hash;
}

void KeyValueSync::HandleMessage(const transport::MessagePtr& msg_ptr) {
    if (destroy_) {
        return;
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& header = msg_ptr->header;
    //assert(header.type() == common::kSyncMessage);
//     SHARDORA_DEBUG("key value sync message coming req: %d, res: %d",
//         header.sync_proto().has_sync_value_req(),
//         header.sync_proto().has_sync_value_res());
    uint32_t queue_size = 0;
    {
        std::lock_guard<std::mutex> lock(kv_msg_mutex_);
        kv_msg_queue_.push(msg_ptr);
        queue_size = static_cast<uint32_t>(kv_msg_queue_.size());
    }
    SHARDORA_DEBUG("queue size kv_msg_queue_: %d, hash: %lu",
        queue_size, msg_ptr->header.hash64());
    wait_con_.notify_one();
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

uint32_t KeyValueSync::PopKvMessage() {
    // Legacy fallback — no longer used. All kv_msg_queue_ consumption is
    // handled by KvConsumerLoop which relays to ready queues.
    // ConsensusTimerMessage drains those queues directly.
    return 0;
}

void KeyValueSync::KvConsumerLoop() {
    // This thread's sole job is to relay messages from kv_msg_queue_ (fed by
    // network threads) into ready queues as fast as possible.
    //
    // ALL actual processing (ProcessSyncValueRequest, ProcessSyncValueResponse)
    // must happen on the timer thread because:
    //   - ProcessSyncValueResponse writes non-thread-safe shared state
    //   - ProcessSyncValueRequest calls hotstuff_mgr_->chain()->GetViewBlockWithHash()
    //     which pops from a SPSC ReaderWriterQueue that the timer thread also pops
    //
    // By keeping this thread as a pure relay, we decouple the network push rate
    // from the timer's processing rate without introducing any thread-safety issues.
    common::GlobalInfo::Instance()->get_thread_index();
    while (!destroy_) {
        uint32_t drained = 0;
        while (drained < kConsumerBatchSize) {
            transport::MessagePtr msg_ptr = nullptr;
            uint32_t kv_msg_size = 0;
            {
                std::lock_guard<std::mutex> lock(kv_msg_mutex_);
                if (kv_msg_queue_.empty()) {
                    break;
                }
                msg_ptr = kv_msg_queue_.front();
                kv_msg_queue_.pop();
                kv_msg_size = static_cast<uint32_t>(kv_msg_queue_.size());
            }

            if (msg_ptr == nullptr) {
                continue;
            }
            const bool is_res = msg_ptr->header.sync_proto().has_sync_value_res();
            if (is_res) {
                kv_ready_res_queue_.push(msg_ptr);
            } else {
                kv_ready_req_queue_.push(msg_ptr);
            }
            SHARDORA_DEBUG("KvConsumerLoop relayed message hash: %lu, kv_msg_queue_ size: %u, "
                "ready_res size: %u, ready_req size: %u, is_res: %d",
                msg_ptr->header.hash64(),
                kv_msg_size,
                (uint32_t)kv_ready_res_queue_.size(),
                (uint32_t)kv_ready_req_queue_.size(),
                is_res);
            ++drained;
        }

        if (drained > 0) {
            uint32_t kv_msg_size = 0;
            {
                std::lock_guard<std::mutex> lock(kv_msg_mutex_);
                kv_msg_size = static_cast<uint32_t>(kv_msg_queue_.size());
            }
            SHARDORA_DEBUG("KvConsumerLoop relayed %u messages, kv_msg remaining: %u, "
                "ready_res: %u, ready_req: %u",
                drained,
                kv_msg_size,
                (uint32_t)kv_ready_res_queue_.size(),
                (uint32_t)kv_ready_req_queue_.size());
            if (drained >= kConsumerBatchSize) {
                continue;
            }
        }

        std::unique_lock<std::mutex> lock(kv_msg_mutex_);
        wait_con_.wait_for(lock, std::chrono::milliseconds(5), [this]() {
            return destroy_ || !kv_msg_queue_.empty();
        });
    }
}

void KeyValueSync::HandleKvMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    SHARDORA_DEBUG("handle kv message hash: %lu, sync_req: %d, sync_res: %d",
        header.hash64(),
        header.sync_proto().has_sync_value_req(),
        header.sync_proto().has_sync_value_res());
    if (header.sync_proto().has_sync_value_req()) {
        ProcessSyncValueRequest(msg_ptr);
    }

    if (header.sync_proto().has_sync_value_res()) {
        ProcessSyncValueResponse(msg_ptr);
    }
}

void KeyValueSync::EnqueueVerifyBlock(
        const ViewBlockPtr& pb_vblock,
        const std::string& key,
        uint32_t tag,
        bool is_broadcast,
        uint64_t msg_hash) {
    if (!pb_vblock) {
        return;
    }

    VerifyBlockItem item;
    item.pb_vblock = pb_vblock;
    item.key = key;
    item.tag = tag;
    item.is_broadcast = is_broadcast;
    item.msg_hash = msg_hash;
    item.enqueue_tm_ms = common::TimeUtils::TimestampMs();
    uint32_t verify_queue_size = 0;
    {
        std::lock_guard<std::mutex> lock(verify_mutex_);
        if (!item.key.empty() && verifying_keys_.find(item.key) != verifying_keys_.end()) {
            return;
        }
        if (!item.key.empty()) {
            verifying_keys_.insert(item.key);
        }
        verify_block_queue_.push(item);
        verify_queue_size = static_cast<uint32_t>(verify_block_queue_.size());
    }

    SHARDORA_DEBUG("enqueue verify block: %u_%u_%lu, height: %lu, key: %s, "
        "hash64: %lu, verify queue: %u, verifying: %u",
        pb_vblock->qc().network_id(),
        pb_vblock->qc().pool_index(),
        pb_vblock->qc().view(),
        pb_vblock->block_info().height(),
        (tag == kBlockHeight ? key.c_str() : common::Encode::HexEncode(key).c_str()),
        msg_hash,
        verify_queue_size,
        verifying_count_.load());
    verify_con_.notify_one();
}

void KeyValueSync::VerifyConsumerLoop() {
    common::GlobalInfo::Instance()->get_thread_index();
    while (!destroy_) {
        VerifyBlockItem item;
        {
            std::unique_lock<std::mutex> lock(verify_mutex_);
            verify_con_.wait_for(lock, std::chrono::milliseconds(5), [this]() {
                return destroy_ || !verify_block_queue_.empty();
            });
            if (destroy_) {
                break;
            }
            if (verify_block_queue_.empty()) {
                continue;
            }

            item = verify_block_queue_.front();
            verify_block_queue_.pop();
        }

        VerifyBlockResult result;
        result.pb_vblock = item.pb_vblock;
        result.key = item.key;
        result.tag = item.tag;
        result.is_broadcast = item.is_broadcast;
        result.msg_hash = item.msg_hash;
        result.enqueue_tm_ms = item.enqueue_tm_ms;
        result.verify_res = -1;

        auto verify_begin_ms = common::TimeUtils::TimestampMs();
        verifying_count_.fetch_add(1);
        if (view_block_synced_callback_ && item.pb_vblock) {
            result.verify_res = view_block_synced_callback_(*item.pb_vblock);
        }
        verifying_count_.fetch_sub(1);
        result.verify_cost_ms = common::TimeUtils::TimestampMs() - verify_begin_ms;

        uint32_t verified_queue_size = 0;
        {
            std::lock_guard<std::mutex> lock(verify_mutex_);
            verified_block_queue_.push(result);
            verified_queue_size = static_cast<uint32_t>(verified_block_queue_.size());
        }

        auto wait_cost_ms = verify_begin_ms >= item.enqueue_tm_ms ?
            verify_begin_ms - item.enqueue_tm_ms : 0;
        if (item.pb_vblock) {
            SHARDORA_DEBUG("verify synced view block done: %u_%u_%lu, height: %lu, "
                "res: %d, wait: %lu ms, verify: %lu ms, hash64: %lu, verified queue: %u",
                item.pb_vblock->qc().network_id(),
                item.pb_vblock->qc().pool_index(),
                item.pb_vblock->qc().view(),
                item.pb_vblock->block_info().height(),
                result.verify_res,
                wait_cost_ms,
                result.verify_cost_ms,
                item.msg_hash,
                verified_queue_size);
        }
    }
}

void KeyValueSync::DrainVerifiedBlocks() {
    uint32_t drained = 0;
    while (drained < kMaxVerifiedDrainCount) {
        VerifyBlockResult result;
        {
            std::lock_guard<std::mutex> lock(verify_mutex_);
            if (verified_block_queue_.empty()) {
                break;
            }
            result = verified_block_queue_.front();
            verified_block_queue_.pop();
        }

        ApplyVerifiedBlockResult(result);
        ++drained;
    }

    if (drained > 0) {
        uint32_t verify_queue_size = 0;
        uint32_t verified_queue_size = 0;
        {
            std::lock_guard<std::mutex> lock(verify_mutex_);
            verify_queue_size = static_cast<uint32_t>(verify_block_queue_.size());
            verified_queue_size = static_cast<uint32_t>(verified_block_queue_.size());
        }
        SHARDORA_DEBUG("DrainVerifiedBlocks drained: %u, verify queue: %u, "
            "verified queue: %u, verifying: %u",
            drained,
            verify_queue_size,
            verified_queue_size,
            verifying_count_.load());
    }
}

void KeyValueSync::ApplyVerifiedBlockResult(const VerifyBlockResult& result) {
    auto& pb_vblock = result.pb_vblock;
    if (!pb_vblock) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(verify_mutex_);
        verifying_keys_.erase(result.key);
    }

    if (result.verify_res == -1) {
        SHARDORA_DEBUG("failed verify synced view block: %u_%u_%lu, height: %lu, "
            "key: %s, is broadcast: %d, verify: %lu ms, hash64: %lu",
            pb_vblock->qc().network_id(),
            pb_vblock->qc().pool_index(),
            pb_vblock->qc().view(),
            pb_vblock->block_info().height(),
            (result.tag == kBlockHeight ? result.key.c_str() : common::Encode::HexEncode(result.key).c_str()),
            result.is_broadcast,
            result.verify_cost_ms,
            result.msg_hash);
        return;
    }

    if (result.verify_res == 2) {
        responsed_keys_.add(result.key);
        synced_map_.erase(result.key);
        return;
    }

    {
        auto& height_map = synced_res_map_[pb_vblock->qc().network_id()][pb_vblock->qc().pool_index()];
        const auto& vblock_hash = pb_vblock->qc().view_block_hash();
        auto& hash_map = height_map[pb_vblock->block_info().height()];
        bool is_new_entry = (hash_map.find(vblock_hash) == hash_map.end());
        auto& entry = hash_map[vblock_hash];
        entry.verified = (result.verify_res == 0);
        entry.pb_vblock = pb_vblock;
        entry.last_attempt_tm_us = common::TimeUtils::TimestampUs();
        if (is_new_entry) {
            entry.recv_tm_us = entry.last_attempt_tm_us;
        }

        if (!entry.verified) {
            // Count the failure so a candidate that can never verify stops
            // blocking its height.  A missing parent is a common transient
            // cause, which is why this is a retry cap and not an immediate
            // retire.
            ++entry.verify_fail_count;
            if (entry.verify_fail_count >= kMaxVerifyFailCount) {
                entry.dead = true;
                SHARDORA_DEBUG("retire synced block after %u failed verifies: %u_%u_%lu "
                    "height: %lu, hash: %s",
                    entry.verify_fail_count,
                    pb_vblock->qc().network_id(),
                    pb_vblock->qc().pool_index(),
                    pb_vblock->qc().view(),
                    pb_vblock->block_info().height(),
                    common::Encode::HexEncode(vblock_hash).c_str());
            }
        } else {
            entry.verify_fail_count = 0;
            entry.dead = false;
        }

        if (pb_vblock->qc().network_id() != network::kRootCongressNetworkId && is_new_entry) {
            ++not_root_synced_res_map_count_;
        }
    }

    if (result.verify_res != 0) {
        SHARDORA_DEBUG("failed check viewblock handle network new view "
            "block: %u_%u_%lu, height: %lu key: %s, is broadcast: %d, "
            "verify: %lu ms, hash64: %lu",
            pb_vblock->qc().network_id(),
            pb_vblock->qc().pool_index(),
            pb_vblock->qc().view(),
            pb_vblock->block_info().height(),
            (result.tag == kBlockHeight ? result.key.c_str() : common::Encode::HexEncode(result.key).c_str()),
            result.is_broadcast,
            result.verify_cost_ms,
            result.msg_hash);
        return;
    }

    SHARDORA_DEBUG("0 success handle network new view block: %u_%u_%lu, height: %lu key: %s, "
        "is broadcast: %d, not_root_synced_res_map_count_: %lu, verify: %lu ms, hash64: %lu",
        pb_vblock->qc().network_id(),
        pb_vblock->qc().pool_index(),
        pb_vblock->qc().view(),
        pb_vblock->block_info().height(),
        (result.tag == kBlockHeight ? result.key.c_str() : common::Encode::HexEncode(result.key).c_str()),
        result.is_broadcast,
        not_root_synced_res_map_count_,
        result.verify_cost_ms,
        result.msg_hash);
    EnqueueVerifiedBlock(pb_vblock);
    QueueFollowupBlockSync(
        pb_vblock->qc().network_id(),
        pb_vblock->qc().pool_index(),
        pb_vblock->block_info().height());
    responsed_keys_.add(result.key);
    synced_map_.erase(result.key);
}

void KeyValueSync::EnqueueVerifiedBlock(const ViewBlockPtr& pb_vblock) {
    if (!pb_vblock) {
        return;
    }

    auto network_id = pb_vblock->qc().network_id();
    auto thread_idx = transport::TcpTransport::Instance()->GetThreadIndexWithPool(
        pb_vblock->qc().pool_index());
    if (!network::IsSameShardOrSameWaitingPool(
            network::kRootCongressNetworkId, network_id) &&
            !network::IsSameToLocalShard(network_id)) {
        thread_idx = transport::TcpTransport::Instance()->GetThreadIndexWithPool(network_id);
    }

    vblock_queues_[thread_idx].push(pb_vblock);
    auto queue_size = vblock_queues_[thread_idx].size();
    SHARDORA_DEBUG("enqueue verified block to hotstuff: %u_%u_%lu, height: %lu, "
        "thread_idx: %u, vblock queue: %lu",
        pb_vblock->qc().network_id(),
        pb_vblock->qc().pool_index(),
        pb_vblock->qc().view(),
        pb_vblock->block_info().height(),
        thread_idx,
        queue_size);
}

void KeyValueSync::ProcessSyncValueRequest(const transport::MessagePtr& msg_ptr) {
    auto& sync_msg = msg_ptr->header.sync_proto();
    //assert(sync_msg.has_sync_value_req());
    // Drain cached_block_queue_ for all chains that may be queried below.
    // This must happen here (on the sync timer thread, the sole consumer)
    // rather than inside GetViewBlockWithHeight/GetViewBlockWithView,
    // because the underlying ReaderWriterQueue is SPSC and those methods
    // can be called from multiple threads.
    for (uint32_t i = 0; i <= common::kImmutablePoolSize; ++i) {
        hotstuff_mgr_->chain(i)->DrainCachedBlockQueue();
    }

    transport::protobuf::Header msg;
    protobuf::SyncMessage& res_sync_msg = *msg.mutable_sync_proto();
    auto sync_res = res_sync_msg.mutable_sync_value_res();
    uint32_t add_size = 0;
    SHARDORA_DEBUG("handle sync value request hash: %lu, key size: %u, height size: %u", 
        msg_ptr->header.hash64(), 
        sync_msg.sync_value_req().keys_size(),
        sync_msg.sync_value_req().heights_size());
    defer({
        SHARDORA_DEBUG("over handle sync value request hash: %lu, key size: %u, height size: %u", 
            msg_ptr->header.hash64(), 
            sync_msg.sync_value_req().keys_size(),
            sync_msg.sync_value_req().heights_size());
    });

    for (int32_t i = 0; i < sync_msg.sync_value_req().keys_size() && add_size < kSyncPacketMaxSize; ++i) {
        const std::string& key = sync_msg.sync_value_req().keys(i);
        SHARDORA_DEBUG("now handle sync view bock hash key: %s", 
            common::Encode::HexEncode(key).c_str());
        if (key.size() != 34) {
            continue;
        }

        uint16_t* pool_index_arr = (uint16_t*)key.c_str();
        // Use remove=false: sync requests only need to look up blocks, not drain
        // the cached_block_queue_. Draining with remove=true from the timer thread
        // causes data races on the SPSC queue and non-thread-safe maps that are
        // owned by the consensus thread.
        auto view_block_ptr_info = hotstuff_mgr_->chain(pool_index_arr[0])->GetViewBlockWithHash(
            std::string(key.c_str() + 2, 32),
            false);
        if (!view_block_ptr_info) {
            continue;
        }
        
        auto view_block_ptr= view_block_ptr_info->view_block;
        if (view_block_ptr != nullptr && !view_block_ptr->qc().sign_x().empty()) {
            SHARDORA_DEBUG("success get view block request coming: %u_%u view block hash: %s, hash: %lu",
                common::GlobalInfo::Instance()->network_id(),
                pool_index_arr[0],
                common::Encode::HexEncode(std::string(key.c_str() + 2, 32)).c_str(),
                msg_ptr->header.hash64());
            auto res = sync_res->add_res();
            res->set_network_id(view_block_ptr->qc().network_id());
            res->set_pool_idx(view_block_ptr->qc().pool_index());
            // `height` used to be filled with qc().view() on this path, which
            // made height and view indistinguishable to the requester. Keep
            // them separate and report the block's real identity as well.
            res->set_height(view_block_ptr->block_info().height());
            res->set_view(view_block_ptr->qc().view());
            res->set_view_block_hash(view_block_ptr->qc().view_block_hash());
            res->set_value(SerializeDeterministic(*view_block_ptr));
            res->set_key(key);
            res->set_tag(kViewHash);
            add_size += 16 + res->value().size();
        } else {
            SHARDORA_DEBUG("failed get view block request coming: %u_%u view block hash: %s, hash: %lu",
                common::GlobalInfo::Instance()->network_id(),
                pool_index_arr[0],
                common::Encode::HexEncode(std::string(key.c_str() + 2, 32)).c_str(),
                msg_ptr->header.hash64());
        }
    }

    auto network_id = sync_msg.sync_value_req().network_id();
    for (int32_t i = 0; i < sync_msg.sync_value_req().heights_size() && add_size < kSyncPacketMaxSize; ++i) {
        auto& req_height = sync_msg.sync_value_req().heights(i);
        std::shared_ptr<view_block::protobuf::ViewBlockItem> view_block_ptr = nullptr;
        if (req_height.has_view_block_hash()) {
            // The requester named the exact block it wants.  Answer with that
            // block or with nothing: falling back to "whatever is at this
            // height" is precisely how a fork sibling used to get returned and
            // then discarded by the requester as a duplicate.
            auto info = hotstuff_mgr_->chain(req_height.pool_idx())->GetViewBlockWithHash(
                req_height.view_block_hash(), false);
            if (info) {
                view_block_ptr = info->view_block;
            }

            if (!view_block_ptr) {
                SHARDORA_DEBUG("sync key value %u_%u_%lu, no block for hash %s, net: %u, pool: %u, hash: %lu",
                    network_id,
                    req_height.pool_idx(),
                    req_height.height(),
                    common::Encode::HexEncode(req_height.view_block_hash()).c_str(),
                    network_id,
                    req_height.pool_idx(),
                    msg_ptr->header.hash64());
                continue;
            }
        } else if (req_height.has_view()) {
            // Fork disambiguation: height + view identifies one branch.
            view_block_ptr = hotstuff_mgr_->chain(req_height.pool_idx())->GetViewBlockWithHeightAndView(
                network_id, req_height.height(), req_height.view());
            if (!view_block_ptr) {
                SHARDORA_DEBUG("sync key value %u_%u_%lu, no block for view %lu, net: %u, pool: %u, hash: %lu",
                    network_id,
                    req_height.pool_idx(),
                    req_height.height(),
                    req_height.view(),
                    network_id,
                    req_height.pool_idx(),
                    msg_ptr->header.hash64());
                continue;
            }
        } else if (req_height.tag() == kBlockHeight) {
            view_block_ptr = hotstuff_mgr_->chain(req_height.pool_idx())->GetViewBlockWithHeight(
                network_id, req_height.height());
            if (!view_block_ptr) {
                SHARDORA_DEBUG("sync key value %u_%u_%lu, handle sync value failed request "
                    "net: %u, pool: %u, height: %lu, hash: %lu",
                    network_id, 
                    req_height.pool_idx(),
                    req_height.height(),
                    network_id, 
                    req_height.pool_idx(),
                    req_height.height(),
                    msg_ptr->header.hash64());
                continue;
            }
        }

        if (req_height.tag() == kBlockView) {
            // A view request is a catch-up request: the requester names the view
            // it wants next and is behind by many, so answer with that view plus
            // as many later ones as fit in the packet.  One round trip per view
            // made catch-up take as long as the view gap.
            auto view_chain = hotstuff_mgr_->ChainForNetwork(network_id, req_height.pool_idx());
            if (view_chain == nullptr) {
                SHARDORA_DEBUG("no view chain for net: %u, pool: %u, view: %lu, hash: %lu",
                    network_id,
                    req_height.pool_idx(),
                    req_height.height(),
                    msg_ptr->header.hash64());
                continue;
            }

            std::vector<std::shared_ptr<ViewBlock>> view_blocks;
            view_chain->GetViewBlocksFrom(
                network_id, req_height.height(), kMaxViewPerResponse, &view_blocks);
            // One request key answers several blocks, so it must be echoed on
            // every item: the requester clears its in-flight entry by this key,
            // and it cannot be rebuilt from the block identity (that would name
            // the view received, not the view requested).
            std::string req_view_key = std::to_string(network_id) + "_" +
                std::to_string(req_height.pool_idx()) + "_" +
                std::to_string(req_height.height()) + "_" +
                std::to_string(kBlockView);
            uint32_t view_added = 0;
            for (auto& one_block : view_blocks) {
                if (one_block == nullptr || one_block->qc().sign_x().empty()) {
                    continue;
                }

                auto value = SerializeDeterministic(*one_block);
                if (add_size + 16 + value.size() > kSyncPacketMaxSize) {
                    break;
                }

                auto res = sync_res->add_res();
                res->set_key(req_view_key);
                res->set_network_id(network_id);
                res->set_pool_idx(req_height.pool_idx());
                // Report the block's own identity: the requester dedups on the
                // view/hash it received, not on the view it asked for.
                res->set_height(one_block->block_info().height());
                res->set_view(one_block->qc().view());
                res->set_view_block_hash(one_block->qc().view_block_hash());
                res->set_value(value);
                res->set_tag(kBlockView);
                add_size += 16 + res->value().size();
                ++view_added;
            }

            SHARDORA_DEBUG("view sync answered net: %u, pool: %u, from view: %lu, blocks: %u, "
                "hash64: %lu",
                network_id,
                req_height.pool_idx(),
                req_height.height(),
                view_added,
                msg_ptr->header.hash64());
            continue;
        }

        if (view_block_ptr == nullptr) {
            continue;
        }

        if (view_block_ptr->qc().sign_x().empty()) {
            SHARDORA_DEBUG("empty sign sync key value %u_%u_%lu, handle sync value failed request "
                "net: %u, pool: %u, height: %lu, hash: %lu",
                network_id, 
                req_height.pool_idx(),
                req_height.height(),
                network_id, 
                req_height.pool_idx(),
                req_height.height(),
                msg_ptr->header.hash64());
            //assert(false);
            continue;
        }
        
        auto res = sync_res->add_res();
        res->set_network_id(network_id);
        res->set_pool_idx(req_height.pool_idx());
        res->set_height(req_height.height());
        // Report the block's own identity so the requester can dedup on it
        // rather than on the height it happened to ask for.
        res->set_view(view_block_ptr->qc().view());
        res->set_view_block_hash(view_block_ptr->qc().view_block_hash());
        res->set_value(SerializeDeterministic(*view_block_ptr));
        res->set_tag(req_height.tag());
        add_size += 16 + res->value().size();
        // Piggyback the pool's latest QC/TC so the requesting node can commit
        // the synced block even when no successor block will ever arrive (e.g.
        // the view timed out and the TC references a proposed-but-uncommitted block).
        auto hf = hotstuff_mgr_->hotstuff(req_height.pool_idx());
        if (hf) {
            auto latest_qc = hf->latest_qc_item_ptr();
            if (latest_qc && latest_qc->view() > view_block_ptr->qc().view() &&
                    latest_qc->has_view_block_hash()) {
                res->set_latest_qc_item(SerializeDeterministic(*latest_qc));
                // Only include the referenced block when it has no sign_x (TC scenario:
                // view timed out, block was proposed but never QC'd).  If it has sign_x
                // the client can sync it through the normal height-sync path.
                auto ref_chain = hotstuff_mgr_->chain(req_height.pool_idx());
                if (ref_chain) {
                    auto ref_info = ref_chain->Get(latest_qc->view_block_hash());
                    if (ref_info && ref_info->view_block &&
                            ref_info->view_block->qc().sign_x().empty()) {
                        res->set_qc_view_block(SerializeDeterministic(*ref_info->view_block));
                    }
                }
                add_size += res->latest_qc_item().size() + res->qc_view_block().size();
            }
        }
    }

    if (sync_msg.sync_value_req().has_latest_sync_item() && add_size < kSyncPacketMaxSize) {
        auto& latest_sync_item = sync_msg.sync_value_req().latest_sync_item();
        SHARDORA_DEBUG("handle sync value latest_sync_item request hash: %lu, net: %u, "
            "globl_pool_height: %lu, pool_latest_heights size: %u, des net: %u, info: %s",
            msg_ptr->header.hash64(),
            network_id,
            sync_msg.sync_value_req().latest_sync_item().globl_pool_height(),
            sync_msg.sync_value_req().latest_sync_item().pool_latest_heights_size(),
            latest_sync_item.network_id(),
            ProtobufToJson(latest_sync_item).c_str());
        if (network::IsSameToLocalShard(latest_sync_item.network_id())) {
            std::shared_ptr<view_block::protobuf::ViewBlockItem> view_block_ptr = nullptr;
            if (latest_sync_item.has_globl_pool_height()) {
                view_block_ptr = hotstuff_mgr_->chain(common::kGlobalPoolIndex)->GetViewBlockWithHeight(
                    network_id, latest_sync_item.globl_pool_height());
                if (view_block_ptr && !view_block_ptr->qc().sign_x().empty()) {
                    auto res = sync_res->add_res();
                    res->set_network_id(network_id);
                    res->set_pool_idx(common::kGlobalPoolIndex);
                    res->set_height(latest_sync_item.globl_pool_height());
                    res->set_view(view_block_ptr->qc().view());
                    res->set_view_block_hash(view_block_ptr->qc().view_block_hash());
                    res->set_value(SerializeDeterministic(*view_block_ptr));
                    res->set_tag(kBlockHeight);
                    add_size += 16 + res->value().size();
                }
            }

            if (latest_sync_item.pool_latest_heights_size() == (int)common::kImmutablePoolSize) {
                uint64_t next_heights[common::kImmutablePoolSize] = { 0 };
                bool active_pools[common::kImmutablePoolSize] = { false };
                for (int32_t i = 0; i < latest_sync_item.pool_latest_heights_size(); ++i) {
                    auto start_height = latest_sync_item.pool_latest_heights(i);
                    if (start_height != common::kInvalidUint64) {
                        next_heights[i] = start_height;
                        active_pools[i] = true;
                    }
                }

                for (uint32_t round = 0;
                        round < kLatestSyncBlocksPerPool && add_size < kSyncPacketMaxSize;
                        ++round) {
                    bool added_in_round = false;
                    for (int32_t i = 0;
                            i < latest_sync_item.pool_latest_heights_size() &&
                            add_size < kSyncPacketMaxSize;
                            ++i) {
                        if (!active_pools[i]) {
                            continue;
                        }

                        auto height = next_heights[i];
                        view_block_ptr = hotstuff_mgr_->chain(i)->GetViewBlockWithHeight(
                            network_id, height);
                        if (!view_block_ptr || view_block_ptr->qc().sign_x().empty()) {
                            active_pools[i] = false;
                            continue;
                        }

                        auto value = SerializeDeterministic(*view_block_ptr);
                        if (add_size + 16 + value.size() > kSyncPacketMaxSize) {
                            break;
                        }

                        auto res = sync_res->add_res();
                        res->set_network_id(network_id);
                        res->set_pool_idx(i);
                        res->set_height(height);
                        res->set_view(view_block_ptr->qc().view());
                        res->set_view_block_hash(view_block_ptr->qc().view_block_hash());
                        res->set_value(value);
                        res->set_tag(kBlockHeight);
                        add_size += 16 + res->value().size();
                        ++next_heights[i];
                        added_in_round = true;
                    }

                    if (!added_in_round) {
                        break;
                    }
                }
            }
        }
    }

    if (add_size == 0) {
        return;
    }

    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kSyncMessage);
    transport::TcpTransport::Instance()->SetMessageHash(msg);

    // Final size guard: if the serialized message exceeds the transport limit,
    // trim response entries until it fits.
    static const uint32_t kMaxSendBytes = (uint32_t)(common::kMaxProposeMsgBytes * 3 / 2) - 4096;
    while (sync_res->res_size() > 0) {
        size_t msg_size = msg.ByteSizeLong();
        if (msg_size <= kMaxSendBytes) {
            break;
        }
        SHARDORA_WARN("sync response too large: %zu bytes > %u limit, trimming last entry (remaining: %d)",
            msg_size, kMaxSendBytes, sync_res->res_size() - 1);
        sync_res->mutable_res()->RemoveLast();
    }

    if (sync_res->res_size() == 0) {
        return;
    }

    SHARDORA_DEBUG("sync response ok des: %u, src hash64: %lu, des hash64: %lu, size: %u, msg size: %lu",
        msg_ptr->header.src_sharding_id(), msg_ptr->header.hash64(), 
        msg.hash64(), add_size, msg.ByteSizeLong());
    transport::TcpTransport::Instance()->Send(msg_ptr->conn->PeerIp(), msg_ptr->conn->PeerPort(), msg);
}

void KeyValueSync::ProcessSyncValueResponse(const transport::MessagePtr& msg_ptr) {
    auto& sync_msg = msg_ptr->header.sync_proto();
    //assert(sync_msg.has_sync_value_res());
    auto& res_arr = sync_msg.sync_value_res().res();
    SHARDORA_DEBUG("now handle kv response hash64: %lu", msg_ptr->header.hash64());
    for (auto iter = res_arr.begin(); iter != res_arr.end(); ++iter) {
        // Request-side key, used only to clear the in-flight entry in
        // synced_map_ once the answer lands.  A height request carries no key
        // on the wire, so it has to be rebuilt here — and it must be rebuilt in
        // the same shape PopItems used, suffix included, or the in-flight entry
        // never gets cleared.  Two shapes exist: an identity probe (view/hash
        // suffix from SyncItem) and a bare height probe (no suffix).
        std::string req_key = iter->key();
        if (iter->tag() == kBlockView) {
            // A kBlockView request is answered with several blocks at different
            // views, all sharing one request key.  The responder echoes that key
            // back; prefer it, since the block's own view would rebuild the wrong
            // key and the in-flight entry would never clear.
            if (req_key.empty()) {
                req_key = std::to_string(iter->network_id()) + "_" +
                    std::to_string(iter->pool_idx()) + "_" +
                    std::to_string(iter->height()) + "_" +
                    std::to_string(kBlockView);
            }
        } else if (iter->tag() == kBlockHeight) {
            req_key = std::to_string(iter->network_id()) + "_" +
                std::to_string(iter->pool_idx()) + "_" +
                std::to_string(iter->height()) + "_" +
                std::to_string(iter->tag());
            if (iter->has_view()) {
                req_key += "_v" + std::to_string(iter->view());
            }

            if (iter->has_view_block_hash()) {
                req_key += "_h" + common::Encode::HexEncode(iter->view_block_hash());
            }
        } else if (iter->tag() == kViewHash) {
            // AddSyncViewHash builds this key as [u16 pool_idx][raw hash], and
            // the responder echoes it back verbatim via res->set_key(key), so
            // iter->key() already is the SyncItem key.  Leave it alone.
        }

        do {
            SHARDORA_DEBUG("now handle kv response hash64: %lu, key: %s, tag: %d",
                msg_ptr->header.hash64(),
                (iter->tag() != kViewHash ? req_key.c_str() : common::Encode::HexEncode(req_key).c_str()),
                iter->tag());
            auto pb_vblock = std::make_shared<view_block::protobuf::ViewBlockItem>();
            if (!pb_vblock->ParseFromString(iter->value())) {
                SHARDORA_ERROR("pb vblock parse failed: %s", req_key.c_str());
                // //assert(false);
                break;
            }

            if (!pb_vblock->has_qc() || pb_vblock->qc().sign_x().empty()) {
                SHARDORA_ERROR("pb vblock has no qc");
                //assert(false);
                break;
            }

            if (pb_vblock->block_info().chain_id() != hotstuff::kGlobalChainId) {
                SHARDORA_ERROR("pb vblock parse failed chain id invalid: %lu, %lu",
                    pb_vblock->block_info().chain_id(), hotstuff::kGlobalChainId);
                break;
            }

            // Everything below keys on the block's real identity, not on the
            // requested height.  That is the whole point of this change: the
            // same height can be answered with two different views, and both
            // must survive to verification so consensus can arbitrate.
            const std::string block_key = SyncedBlockKey(
                pb_vblock->qc().network_id(),
                pb_vblock->qc().pool_index(),
                pb_vblock->qc().view_block_hash());

            // Skip re-verification only when this exact block is already in
            // synced_res_map_ or has already been answered.  A sibling view at
            // the same height is a different block and must go through.
            {
                auto net_iter = synced_res_map_.find(pb_vblock->qc().network_id());
                if (net_iter != synced_res_map_.end()) {
                    auto pool_iter = net_iter->second.find(pb_vblock->qc().pool_index());
                    if (pool_iter != net_iter->second.end()) {
                        auto height_iter = pool_iter->second.find(pb_vblock->block_info().height());
                        if (height_iter != pool_iter->second.end() &&
                                height_iter->second.find(pb_vblock->qc().view_block_hash()) !=
                                height_iter->second.end()) {
                            SHARDORA_DEBUG("skip re-verify already synced block: %u_%u_%lu height: %lu",
                                pb_vblock->qc().network_id(), pb_vblock->qc().pool_index(),
                                pb_vblock->qc().view(), pb_vblock->block_info().height());
                            break;
                        }
                    }
                }

                if (responsed_keys_.exists(block_key)) {
                    SHARDORA_DEBUG("skip re-verify already responsed block: %s",
                        common::Encode::HexEncode(block_key).c_str());
                    break;
                }
            }

            // Clear the in-flight entry unless this was an identity probe that
            // got a different branch back.  An identity probe names one exact
            // block, so a sibling at the same height does not answer it and the
            // entry must stay for a retry against another peer.  A bare height
            // probe is satisfied by any answer: that is the point of asking
            // without an identity.
            bool identity_probe = req_key.find("_h") != std::string::npos;
            if (!identity_probe || req_key == block_key) {
                synced_map_.erase(req_key);
            }

            // Attach piggybacked TC/QC data so HandleSyncedViewBlock can commit the
            // synced block even when no successor block will ever arrive.
            if (iter->has_latest_qc_item()) {
                pb_vblock->set_sync_tc_item(iter->latest_qc_item());
                if (iter->has_qc_view_block()) {
                    pb_vblock->set_sync_tc_ref_block(iter->qc_view_block());
                }
            }
            EnqueueVerifyBlock(
                pb_vblock,
                block_key,
                iter->tag(),
                iter->key().empty(),
                msg_ptr->header.hash64());
        } while (0);

        SHARDORA_DEBUG("block response coming: %s, sync map size: %u, hash64: %lu",
            req_key.c_str(), synced_map_.size(), msg_ptr->header.hash64());
    }

    {
        uint32_t drained = 0;
        static const uint32_t kMaxInlineDrain = 128;
        for (auto net_iter = synced_res_map_.begin();
                net_iter != synced_res_map_.end() && drained < kMaxInlineDrain; ++net_iter) {
            auto network_id = net_iter->first;
            for (auto pool_iter = net_iter->second.begin();
                    pool_iter != net_iter->second.end() && drained < kMaxInlineDrain; ++pool_iter) {
                auto pool_idx = pool_iter->first;
                uint64_t latest_height;
                if (network_id == network::kRootCongressNetworkId &&
                        !network::IsSameToLocalShard(network_id)) {
                    latest_height = tx_pool_mgr_->root_latest_height(pool_idx);
                } else if (network::IsSameToLocalShard(network_id)) {
                    latest_height = tx_pool_mgr_->latest_height(pool_idx);
                } else {
                    latest_height = tx_pool_mgr_->cross_latest_height(network_id);
                }

                // The block the local node already committed at latest_height is
                // the authoritative choice for any fork at that height, so drop
                // everything below it and let the run advance from there.
                drained += DrainConsecutiveHeights(
                    network_id,
                    pool_idx,
                    latest_height,
                    &pool_iter->second,
                    kMaxInlineDrain - drained);
            }
        }

        if (drained > 0) {
            SHARDORA_DEBUG("inline drain pushed %u blocks to consensus", drained);
        }
    }
}

void KeyValueSync::HandlerVerifiedBlock(const std::map<uint32_t, std::map<uint32_t, std::map<uint64_t, std::shared_ptr<view_block::protobuf::ViewBlockItem>>>>& res_map) {
    for (auto iter = res_map.begin(); iter != res_map.end(); ++iter) {
        auto network_id = iter->first;
        for (auto pool_iter = iter->second.begin(); pool_iter != iter->second.end(); ++pool_iter) {
            for (auto iter2 = pool_iter->second.begin(); iter2 != pool_iter->second.end(); ++iter2) {
                auto pb_vblock = iter2->second;
                EnqueueVerifiedBlock(pb_vblock);
                SHARDORA_DEBUG("1 success handle network new view block: %u_%u_%lu, height: %lu",
                    pb_vblock->qc().network_id(),
                    pb_vblock->qc().pool_index(),
                    pb_vblock->qc().view(),
                    pb_vblock->block_info().height());
            }
        }
    }
}

void KeyValueSync::QueueFollowupBlockSync(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height) {
    if (height == common::kInvalidUint64) {
        return;
    }

    if (network_id != network::kRootCongressNetworkId &&
            not_root_synced_res_map_count_ >= kMaxSyncLatestNotRootCount) {
        return;
    }

    auto now_tm_us = common::TimeUtils::TimestampUs();
    for (uint32_t i = 1; i <= kFollowupSyncHeightCount; ++i) {
        auto next_height = height + i;
        std::string key = std::to_string(network_id) + "_" +
            std::to_string(pool_idx) + "_" +
            std::to_string(next_height) + "_" +
            std::to_string(kBlockHeight);

        if (synced_map_.exists(key)) {
            continue;
        }

        // Ask only when the height has no candidate that could still be
        // committed.  A candidate that is present but dead, or one that expired
        // without ever verifying, does not count: treating it as an answer is
        // exactly how a height used to get stuck with nothing usable and no
        // outstanding request.  A live candidate means the height was answered,
        // and the branch continuing from the block just handled is requested
        // through AddSyncViewHash, which names it exactly.
        if (HeightHasLiveCandidate(
                synced_res_map_[network_id][pool_idx], next_height, now_tm_us)) {
            continue;
        }

        auto item = std::make_shared<SyncItem>(
            network_id,
            pool_idx,
            next_height,
            kSyncHighest,
            kBlockHeight);
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        SHARDORA_DEBUG("block height add new sync item key: %s, priority: %u, %u_%u_%lu, "
            "next_height: %lu, kSyncHighest: %u",
            item->key.c_str(), item->priority, network_id, pool_idx,
            static_cast<uint32_t>(kBlockHeight), next_height,
            static_cast<uint32_t>(kSyncHighest));
        item_queues_[thread_idx].push(item);
    }
}


void KeyValueSync::SyncAllLatestBlocks() {
    // return;
    auto local_net_id = common::GlobalInfo::Instance()->network_id();
    auto end_shard = common::GlobalInfo::Instance()->now_valid_end_shard();
    SHARDORA_DEBUG("SyncAllLatestBlocks enter: local_net=%u, end_shard=%u, "
        "synced_res_map size=%lu, not_root_count=%u",
        local_net_id, end_shard,
        synced_res_map_.size(), not_root_synced_res_map_count_);
    // Dump synced_res_map_ contents for debugging
    for (auto& [net, pool_map] : synced_res_map_) {
        for (auto& [pool, height_map] : pool_map) {
            if (!height_map.empty()) {
                auto first_h = height_map.begin()->first;
                auto last_h = height_map.rbegin()->first;
                SHARDORA_DEBUG("  synced_res_map[net=%u][pool=%u]: %lu entries, "
                    "heights=[%lu..%lu]",
                    net, pool, height_map.size(), first_h, last_h);
            }
        }
    }
    std::map<uint32_t, std::map<uint32_t, std::map<uint64_t, std::shared_ptr<view_block::protobuf::ViewBlockItem>>>> res_map;
    std::map<uint32_t, sync::protobuf::SyncMessage> sync_dht_map;
    auto add_sync_item = [&](uint32_t network, uint32_t pool_index, uint64_t height, bool global) {
        if (network != network::kRootCongressNetworkId && not_root_synced_res_map_count_ >= kMaxSyncLatestNotRootCount) {
            return;
        }

        auto iter = sync_dht_map.find(network);
        if (iter == sync_dht_map.end()) {
            sync_dht_map[network] = sync::protobuf::SyncMessage();
            auto* sync_req = sync_dht_map[network].mutable_sync_value_req();
            sync_req->set_network_id(network);
        }

        auto* sync_req = sync_dht_map[network].mutable_sync_value_req();
        auto* sync_latest_req = sync_req->mutable_latest_sync_item();
        sync_latest_req->set_network_id(network);
        if (!global) {
            if (sync_latest_req->pool_latest_heights_size() != (int)common::kImmutablePoolSize) {
                for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
                    sync_latest_req->add_pool_latest_heights(common::kInvalidUint64);
                }
            }

            sync_latest_req->set_pool_latest_heights(pool_index, height);
        } else {
            sync_latest_req->set_globl_pool_height(height);
        }

        SHARDORA_DEBUG("add sync item: %u_%u_%u", network, pool_index, height);
    };

    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        for (uint32_t network_id = network::kRootCongressNetworkId;
                network_id <= common::GlobalInfo::Instance()->now_valid_end_shard(); ++network_id) {
            auto latest_height = tx_pool_mgr_->latest_height(i);
            if (network_id == network::kRootCongressNetworkId) {
                if (!network::IsSameToLocalShard(network_id)) {
                    latest_height = tx_pool_mgr_->root_latest_height(i);
                }
            } else {
                if (!network::IsSameToLocalShard(network_id)) {
                    break;
                }
            }

            // Fix: For active committee members, use committed_height as the
            // baseline instead of tx_pool latest_height. Blocks up to
            // committed_height are already handled — no need to sync them.
            // We do NOT add any look-ahead beyond committed_height because
            // that can cause ChainIsFull() to fail (tx_pool needs continuous
            // heights). If sync fetches a block that consensus also produces,
            // the duplicate is harmlessly discarded.
            if (network::IsSameToLocalShard(network_id) && hotstuff_mgr_) {
                auto chain = hotstuff_mgr_->chain(i);
                if (chain) {
                    auto committed_vb = chain->LatestCommittedBlock();
                    if (committed_vb && committed_vb->has_block_info() &&
                            committed_vb->block_info().height() > latest_height) {
                        latest_height = committed_vb->block_info().height();
                    }
                }
            }

            auto iter = synced_res_map_.find(network_id);
            if (iter == synced_res_map_.end()) {
                add_sync_item(network_id, i, latest_height + 1, i == common::kGlobalPoolIndex);
                continue;
            }

            auto pool_iter = iter->second.find(i);
            if (pool_iter == iter->second.end()) {
                add_sync_item(network_id, i, latest_height + 1, i == common::kGlobalPoolIndex);
                continue;
            }

            SHARDORA_DEBUG("  pool %u net %u: latest_height=%lu, synced entries=%lu",
                i, network_id, latest_height, pool_iter->second.size());

            // Always erase all entries with height <= latest_height, regardless
            // of whether latest_height itself is in the map. The old code only
            // cleaned up when latest_height was present, leaving stale entries
            // when blocks were committed via consensus (not sync), causing
            // not_root_synced_res_map_count_ to grow unboundedly.
            EraseSyncedHeightsUpTo(network_id, i, latest_height, &pool_iter->second);

            ++latest_height;

            // Retry verification for anything still outstanding, then push the
            // run of consecutive heights to consensus.  DrainConsecutiveHeights
            // picks the branch per height (locally committed, else highest view).
            for (auto& height_entry : pool_iter->second) {
                if (height_entry.first < latest_height) {
                    continue;
                }

                for (auto& hash_entry : height_entry.second) {
                    auto& entry = hash_entry.second;
                    if (entry.verified || !entry.pb_vblock || entry.dead) {
                        continue;
                    }

                    // A candidate that already exhausted its retries stays
                    // retired; re-enqueueing it would crowd out the sibling
                    // that SelectHeightEntry wants to try next.
                    if (entry.verify_fail_count >= kMaxVerifyFailCount) {
                        continue;
                    }

                    EnqueueVerifyBlock(
                        entry.pb_vblock,
                        SyncedBlockKey(network_id, i, hash_entry.first),
                        kBlockHeight,
                        false,
                        0);
                }
            }

            DrainConsecutiveHeights(
                network_id,
                i,
                latest_height - 1,
                &pool_iter->second,
                kFollowupSyncHeightCount);

            // Advance to the first height that still needs a block, skipping the
            // heights that already hold something usable.  Skipping to the
            // maximum height instead would walk past every intermediate height
            // whose candidates all died, and those heights would never be
            // requested again — a permanent gap that latest_height can never
            // cross.  Heights already committed are skipped too, so blocks in
            // transit (pushed but not yet committed) are not re-requested.
            {
                auto now_tm_us = common::TimeUtils::TimestampUs();
                auto height_iter = pool_iter->second.lower_bound(latest_height);
                while (height_iter != pool_iter->second.end() &&
                        HeightHasLiveCandidate(
                            pool_iter->second, height_iter->first, now_tm_us)) {
                    latest_height = height_iter->first + 1;
                    height_iter = pool_iter->second.lower_bound(latest_height);
                }
            }

            add_sync_item(network_id, i, latest_height, i == common::kGlobalPoolIndex);
        }
    }

    for (uint32_t network_id = network::kConsensusShardBeginNetworkId;
            network_id <= common::GlobalInfo::Instance()->now_valid_end_shard(); ++network_id) {
        if (network::IsSameToLocalShard(network_id)) {
            continue;
        }

        const uint64_t latest_height_base = tx_pool_mgr_->cross_latest_height(network_id);
        auto iter = synced_res_map_.find(network_id);
        if (iter == synced_res_map_.end()) {
            add_sync_item(network_id, common::kGlobalPoolIndex, latest_height_base + 1, true);
            continue;
        }

        // Responses key synced blocks by real qc().pool_index(), but this loop
        // previously only cleaned common::kGlobalPoolIndex — other pools never
        // got erased, so not_root_synced_res_map_count_ stayed high and grew.
        std::vector<uint32_t> cross_pool_keys;
        cross_pool_keys.reserve(iter->second.size());
        for (const auto& pr : iter->second) {
            cross_pool_keys.push_back(pr.first);
        }
        if (cross_pool_keys.empty()) {
            add_sync_item(network_id, common::kGlobalPoolIndex, latest_height_base + 1, true);
            continue;
        }

        uint64_t max_latest_for_global_sync = latest_height_base;
        for (uint32_t pool_key : cross_pool_keys) {
            auto pool_iter = iter->second.find(pool_key);
            if (pool_iter == iter->second.end()) {
                continue;
            }

            uint64_t latest_height = latest_height_base;
            SHARDORA_DEBUG("  cross pool %u net %u: latest_height=%lu, synced entries=%lu",
                pool_key, network_id, latest_height, pool_iter->second.size());

            EraseSyncedHeightsUpTo(network_id, pool_key, latest_height, &pool_iter->second);

            ++latest_height;

            for (auto& height_entry : pool_iter->second) {
                if (height_entry.first < latest_height) {
                    continue;
                }

                for (auto& hash_entry : height_entry.second) {
                    auto& entry = hash_entry.second;
                    if (entry.verified || !entry.pb_vblock || entry.dead) {
                        continue;
                    }

                    if (entry.verify_fail_count >= kMaxVerifyFailCount) {
                        continue;
                    }

                    EnqueueVerifyBlock(
                        entry.pb_vblock,
                        SyncedBlockKey(network_id, pool_key, hash_entry.first),
                        kBlockHeight,
                        false,
                        0);
                }
            }

            DrainConsecutiveHeights(
                network_id,
                pool_key,
                latest_height - 1,
                &pool_iter->second,
                kFollowupSyncHeightCount);

            {
                auto now_tm_us = common::TimeUtils::TimestampUs();
                auto height_iter = pool_iter->second.lower_bound(latest_height);
                while (height_iter != pool_iter->second.end() &&
                        HeightHasLiveCandidate(
                            pool_iter->second, height_iter->first, now_tm_us)) {
                    latest_height = height_iter->first + 1;
                    height_iter = pool_iter->second.lower_bound(latest_height);
                }
            }

            max_latest_for_global_sync = std::max(max_latest_for_global_sync, latest_height);
        }

        add_sync_item(network_id, common::kGlobalPoolIndex, max_latest_for_global_sync, true);
    }

    HandlerVerifiedBlock(res_map);
    std::set<uint64_t> sended_neigbors;
    uint32_t sent_count = 0;
    for (auto iter = sync_dht_map.begin(); iter != sync_dht_map.end(); ++iter) {
        uint32_t fanout = 1;
        if (not_root_synced_res_map_count_ < kMaxSyncLatestNotRootCount / 2) {
            fanout = kLatestSyncPeerFanout;
        }

        for (uint32_t i = 0; i < fanout; ++i) {
            uint64_t choose_node = SendSyncRequest(
                iter->first,
                iter->second,
                sended_neigbors);
            if (choose_node == 0) {
                break;
            }

            sended_neigbors.insert(choose_node);
            ++sent_count;
        }
    }
    SHARDORA_DEBUG("SyncAllLatestBlocks done: sync_dht_map size=%lu, sent=%u, "
        "res_map size=%lu, fanout max=%u",
        sync_dht_map.size(), sent_count, res_map.size(), kLatestSyncPeerFanout);
}

}  // namespace sync

}  // namespace shardora
