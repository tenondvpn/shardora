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
    SHARDORA_DEBUG("block height add new sync item key: %s, priority: %u, item size: %u",
        common::Encode::HexEncode(item->key).c_str(), 
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
            
            if (item->tag == kBlockHeight) {
                auto iter = synced_res_map_.find(item->network_id);
                if (iter != synced_res_map_.end()) {
                    auto iter2 = iter->second.find(item->pool_idx);
                    if (iter2 != iter->second.end()) {
                        auto iter3 = iter2->second.find(item->height);
                        if (iter3 != iter2->second.end()) {
                            continue;
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
                SHARDORA_DEBUG("try to sync normal block: %u_%u_%lu, tag: %d",
                    item->network_id, item->pool_idx, item->height, item->tag);
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
        bool is_new_entry = (height_map.find(pb_vblock->block_info().height()) == height_map.end());
        height_map[pb_vblock->block_info().height()] = std::make_pair((result.verify_res == 0), pb_vblock);
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
            res->set_height(view_block_ptr->qc().view());
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
        if (req_height.tag() == kBlockHeight) {
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
            view_block_ptr = hotstuff_mgr_->chain(req_height.pool_idx())->GetViewBlockWithView(
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
        res->set_value(SerializeDeterministic(*view_block_ptr));
        res->set_tag(req_height.tag());
        add_size += 16 + res->value().size();
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
        std::string key = iter->key();
        if (iter->tag() == kBlockHeight || iter->tag() == kBlockView) {
            key = std::to_string(iter->network_id()) + "_" +
                std::to_string(iter->pool_idx()) + "_" +
                std::to_string(iter->height()) + "_" +
                std::to_string(iter->tag());
        }

        do {
            SHARDORA_DEBUG("now handle kv response hash64: %lu, key: %s, tag: %d",
                msg_ptr->header.hash64(), 
                (iter->tag() != kViewHash ? key.c_str() : common::Encode::HexEncode(key).c_str()), 
                iter->tag());
            auto pb_vblock = std::make_shared<view_block::protobuf::ViewBlockItem>();
            if (!pb_vblock->ParseFromString(iter->value())) {
                SHARDORA_ERROR("pb vblock parse failed: %s", key.c_str());
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

            // Skip re-verification if this height is already in synced_res_map_.
            // This prevents the verify queue from flooding when latest_height stalls
            // (e.g. vblock_queue backlog) and sync keeps re-requesting the same blocks.
            {
                auto net_iter = synced_res_map_.find(pb_vblock->qc().network_id());
                if (net_iter != synced_res_map_.end()) {
                    auto pool_iter = net_iter->second.find(pb_vblock->qc().pool_index());
                    if (pool_iter != net_iter->second.end() &&
                            pool_iter->second.find(pb_vblock->block_info().height()) != pool_iter->second.end()) {
                        SHARDORA_DEBUG("skip re-verify already synced block: %u_%u_%lu height: %lu",
                            pb_vblock->qc().network_id(), pb_vblock->qc().pool_index(),
                            pb_vblock->qc().view(), pb_vblock->block_info().height());
                        break;
                    }
                }
            }

            EnqueueVerifyBlock(
                pb_vblock,
                key,
                iter->tag(),
                iter->key().empty(),
                msg_ptr->header.hash64());
        } while (0);

        SHARDORA_DEBUG("block response coming: %s, sync map size: %u, hash64: %lu",
            key.c_str(), synced_map_.size(), msg_ptr->header.hash64());
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

                auto height_iter = pool_iter->second.find(latest_height + 1);
                while (height_iter != pool_iter->second.end() && drained < kMaxInlineDrain) {
                    if (height_iter->second.first) {
                        // Already verified, push to consensus
                        auto& pb_vblock = height_iter->second.second;
                        EnqueueVerifiedBlock(pb_vblock);
                        ++drained;
                        ++latest_height;
                        height_iter = pool_iter->second.find(latest_height + 1);
                        continue;
                    }
                    // Not yet verified, retry on verification workers instead
                    // of blocking the kv timer thread on BLS checks.
                    auto& pb_vblock = height_iter->second.second;
                    std::string key = std::to_string(network_id) + "_" +
                        std::to_string(pool_idx) + "_" +
                        std::to_string(pb_vblock->block_info().height()) + "_" +
                        std::to_string(kBlockHeight);
                    EnqueueVerifyBlock(pb_vblock, key, kBlockHeight, false, 0);
                    break; // Can't push this pool until this height verifies.
                }
            }
        }

        if (drained > 0) {
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

    for (uint32_t i = 1; i <= kFollowupSyncHeightCount; ++i) {
        auto next_height = height + i;
        std::string key = std::to_string(network_id) + "_" +
            std::to_string(pool_idx) + "_" +
            std::to_string(next_height) + "_" +
            std::to_string(kBlockHeight);

        if (responsed_keys_.exists(key)) {
            continue;
        }

        if (synced_map_.exists(key)) {
            continue;
        }

        auto net_iter = synced_res_map_.find(network_id);
        if (net_iter != synced_res_map_.end()) {
            auto pool_iter = net_iter->second.find(pool_idx);
            if (pool_iter != net_iter->second.end() &&
                    pool_iter->second.find(next_height) != pool_iter->second.end()) {
                continue;
            }
        }

        auto item = std::make_shared<SyncItem>(
            network_id,
            pool_idx,
            next_height,
            kSyncHighest,
            kBlockHeight);
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
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
            {
                auto erase_end = pool_iter->second.upper_bound(latest_height);
                if (erase_end != pool_iter->second.begin()) {
                    auto now_size = pool_iter->second.size();
                    pool_iter->second.erase(pool_iter->second.begin(), erase_end);
                    if (network_id != network::kRootCongressNetworkId) {
                        not_root_synced_res_map_count_ -= now_size - pool_iter->second.size();
                    }
                }
            }

            auto height_iter = pool_iter->second.find(++latest_height);
            while (height_iter != pool_iter->second.end()) {
                if (!height_iter->second.first) {
                    auto& pb_vblock = height_iter->second.second;
                    std::string key = std::to_string(network_id) + "_" +
                        std::to_string(i) + "_" +
                        std::to_string(pb_vblock->block_info().height()) + "_" +
                        std::to_string(kBlockHeight);
                    EnqueueVerifyBlock(pb_vblock, key, kBlockHeight, false, 0);
                }
                height_iter = pool_iter->second.find(++latest_height);
            }

            // Fix: Skip past ALL heights already in synced_res_map_, not just
            // consecutive ones from latest_height. The map may have gaps (e.g.,
            // heights 68,69 present but 70 missing). We need to request from
            // the first truly missing height, not from the first gap.
            // Also skip heights that were already committed to avoid re-requesting
            // blocks that are in transit (pushed to vblock_queues_ but not yet
            // committed, so latest_height hasn't advanced yet).
            if (!pool_iter->second.empty()) {
                auto max_synced = pool_iter->second.rbegin()->first;
                if (max_synced >= latest_height) {
                    latest_height = max_synced + 1;
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

            {
                auto erase_end = pool_iter->second.upper_bound(latest_height);
                if (erase_end != pool_iter->second.begin()) {
                    auto now_size = pool_iter->second.size();
                    pool_iter->second.erase(pool_iter->second.begin(), erase_end);
                    if (network_id != network::kRootCongressNetworkId) {
                        not_root_synced_res_map_count_ -= now_size - pool_iter->second.size();
                    }
                }
            }

            auto height_iter = pool_iter->second.find(++latest_height);
            while (height_iter != pool_iter->second.end()) {
                if (!height_iter->second.first) {
                    auto& pb_vblock = height_iter->second.second;
                    std::string key = std::to_string(network_id) + "_" +
                        std::to_string(pool_key) + "_" +
                        std::to_string(pb_vblock->block_info().height()) + "_" +
                        std::to_string(kBlockHeight);
                    EnqueueVerifyBlock(pb_vblock, key, kBlockHeight, false, 0);
                }

                height_iter = pool_iter->second.find(++latest_height);
            }

            if (!pool_iter->second.empty()) {
                auto max_synced = pool_iter->second.rbegin()->first;
                if (max_synced >= latest_height) {
                    latest_height = max_synced + 1;
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
