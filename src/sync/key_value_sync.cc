#include "sync/key_value_sync.h"

#include "block/block_manager.h"
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
#include "sync/sync_utils.h"
#include "transport/processor.h"

namespace shardora {

namespace sync {

KeyValueSync::KeyValueSync() {}

KeyValueSync::~KeyValueSync() {
}

void KeyValueSync::Init(
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<consensus::HotstuffManager>& hotstuff_mgr,
        const std::shared_ptr<db::Db>& db,
        ViewBlockSyncedCallback view_block_synced_callback) {
    SHARDORA_DEBUG("init key value sync 0");
    hotstuff_mgr_ = hotstuff_mgr;
    SHARDORA_DEBUG("init key value sync 1");
    view_block_synced_callback_ = view_block_synced_callback;
    SHARDORA_DEBUG("init key value sync 2");
    network::Route::Instance()->RegisterMessage(
        common::kSyncMessage,
        std::bind(&KeyValueSync::HandleMessage, this, std::placeholders::_1));
    SHARDORA_DEBUG("init key value sync 3");
    kv_tick_.CutOff(
        10000lu,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
    SHARDORA_DEBUG("init key value sync 4");
    transport::Processor::Instance()->RegisterProcessor(
        common::kHotstuffSyncTimerMessage,
        std::bind(&KeyValueSync::HotstuffConsensusTimerMessage, this, std::placeholders::_1));    
    SHARDORA_DEBUG("init key value sync 5");
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
    assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(network_id, pool_idx, height, priority);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    item_queues_[thread_idx].push(item);
    SHARDORA_DEBUG("block height add new sync item key: %s, priority: %u",
        item->key.c_str(), item->priority);
}

void KeyValueSync::HotstuffConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    std::shared_ptr<view_block::protobuf::ViewBlockItem> pb_vblock = nullptr;
    while (vblock_queues_[thread_idx].pop(&pb_vblock)) {
        if (pb_vblock) {
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
        }
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
            res->set_height(view_block_ptr->qc().view());
            res->set_value(view_block_ptr->SerializeAsString());
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
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    network::Route::Instance()->Send(msg_ptr);
    SHARDORA_DEBUG("sync global block ok des: %u, des hash64: %lu",
        network::kNodeNetworkId, msg.hash64());
}

void KeyValueSync::AddSyncViewHash(
        uint32_t network_id, 
        uint32_t pool_idx,
        const std::string& view_hash, 
        uint32_t priority) {
    // return;
    assert(!view_hash.empty());
    char key[2 + view_hash.size()] = {0};
    uint16_t* pools = (uint16_t*)(key);
    pools[0] = pool_idx;
    memcpy(key + 2, view_hash.c_str(), view_hash.size());
    assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(
        network_id, std::string(key, sizeof(key)), priority);
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
    auto count = PopKvMessage();
    auto now_tm_ms1 = common::TimeUtils::TimestampMs();
    PopItems();
    auto now_tm_ms2 = common::TimeUtils::TimestampMs();
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        hotstuff_mgr_->chain(i)->GetViewBlockWithHash("");
        hotstuff_mgr_->chain(i)->GetViewBlockWithHeight(0, 0);
    }

    auto now_tm_ms3 = common::TimeUtils::TimestampMs();
    auto etime = common::TimeUtils::TimestampMs();
    if (etime - now_tm_ms >= 1000000lu) {
        SHARDORA_ERROR("KeyValueSync handle message use time: %lu, "
            "PopKvMessage: %lu, PopItems: %lu, CheckSyncItem: %lu", 
            (etime - now_tm_ms), 
            (now_tm_ms1 - now_tm_ms),
            (now_tm_ms2 - now_tm_ms1),
            (now_tm_ms3 - now_tm_ms2));
        // assert(false);
    }

    kv_tick_.CutOff(
        10000lu,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
    // return count;
}

void KeyValueSync::PopItems() {
    std::set<uint64_t> sended_neigbors;
    std::map<uint32_t, sync::protobuf::SyncMessage> sync_dht_map;
    bool stop = false;
    auto now_tm = common::TimeUtils::TimestampUs();
    uint32_t synced_count = 0;
    for (uint8_t thread_idx = 0; thread_idx < common::kMaxThreadCount; ++thread_idx) {
        while (true) {
            SyncItemPtr item = nullptr;
            item_queues_[thread_idx].pop(&item);
            if (item == nullptr) {
                break;
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

            auto sync_req = sync_dht_map[item->network_id].mutable_sync_value_req();
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
            CHECK_MEMORY_SIZE(synced_map_);
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
                break;
            }
        }
    }
}

uint64_t KeyValueSync::SendSyncRequest(
        uint32_t network_id,
        const sync::protobuf::SyncMessage& sync_msg,
        const std::set<uint64_t>& sended_neigbors) {
    std::vector<dht::NodePtr> nodes;
    SHARDORA_DEBUG("now get universal dht 9");
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
    assert(header.type() == common::kSyncMessage);
//     SHARDORA_DEBUG("key value sync message coming req: %d, res: %d",
//         header.sync_proto().has_sync_value_req(),
//         header.sync_proto().has_sync_value_res());
    kv_msg_queue_.push(msg_ptr);
    SHARDORA_DEBUG("queue size kv_msg_queue_: %d, hash: %lu",
        kv_msg_queue_.size(), msg_ptr->header.hash64());
    wait_con_.notify_one();
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

uint32_t KeyValueSync::PopKvMessage() {
    uint32_t count = 0;
    while (count++ < kEachTimerHandleCount) {
        transport::MessagePtr msg_ptr = nullptr;
        if (!kv_msg_queue_.pop(&msg_ptr) || msg_ptr == nullptr) {
            break;
        }

        HandleKvMessage(msg_ptr);
    }

    return count;
}

void KeyValueSync::HandleKvMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    if (header.sync_proto().has_sync_value_req()) {
        ProcessSyncValueRequest(msg_ptr);
    }

    if (header.sync_proto().has_sync_value_res()) {
        ProcessSyncValueResponse(msg_ptr);
    }
}

void KeyValueSync::ProcessSyncValueRequest(const transport::MessagePtr& msg_ptr) {
    auto& sync_msg = msg_ptr->header.sync_proto();
    assert(sync_msg.has_sync_value_req());
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
    for (int32_t i = 0; i < sync_msg.sync_value_req().keys_size(); ++i) {
        const std::string& key = sync_msg.sync_value_req().keys(i);
        SHARDORA_DEBUG("now handle sync view bock hash key: %s", 
            common::Encode::HexEncode(key).c_str());
        if (key.size() != 34) {
            continue;
        }

        uint16_t* pool_index_arr = (uint16_t*)key.c_str();
        auto view_block_ptr = hotstuff_mgr_->chain(pool_index_arr[0])->GetViewBlockWithHash(
            std::string(key.c_str() + 2, 32));
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
            res->set_value(view_block_ptr->SerializeAsString());
            res->set_key(key);
            res->set_tag(kViewHash);
            add_size += 16 + res->value().size();
            SHARDORA_DEBUG("handle sync value view add add_size: %u request hash: %lu, "
                "net: %u, pool: %u, height: %lu",
                add_size,
                msg_ptr->header.hash64(),
                res->network_id(),
                res->pool_idx(),
                res->height());
            if (add_size >= kSyncPacketMaxSize) {
                SHARDORA_DEBUG("handle sync value view add_size failed request hash: %lu, "
                    "net: %u, pool: %u, height: %lu",
                    res->network_id(),
                    res->pool_idx(),
                    res->height(),
                    msg_ptr->header.hash64());
                break;
            }
        } else {
            SHARDORA_DEBUG("failed get view block request coming: %u_%u view block hash: %s, hash: %lu",
                common::GlobalInfo::Instance()->network_id(),
                pool_index_arr[0],
                common::Encode::HexEncode(std::string(key.c_str() + 2, 32)).c_str(),
                msg_ptr->header.hash64());
        }
    }

    auto network_id = sync_msg.sync_value_req().network_id();
    for (int32_t i = 0; i < sync_msg.sync_value_req().heights_size(); ++i) {
        auto& req_height = sync_msg.sync_value_req().heights(i);
        if (req_height.tag() == kBlockHeight) {
            auto view_block_ptr = hotstuff_mgr_->chain(req_height.pool_idx())->GetViewBlockWithHeight(
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
                assert(false);
                continue;
            }
            
            auto res = sync_res->add_res();
            res->set_network_id(network_id);
            res->set_pool_idx(req_height.pool_idx());
            res->set_height(req_height.height());
            res->set_value(view_block_ptr->SerializeAsString());
            res->set_tag(kBlockHeight);
            add_size += 16 + res->value().size();
            if (add_size >= kSyncPacketMaxSize) {
                SHARDORA_DEBUG("handle sync value add_size failed request hash: %lu, "
                    "net: %u, pool: %u, height: %lu",
                    network_id,
                    req_height.pool_idx(),
                    req_height.height(),
                    msg_ptr->header.hash64());
                break;
            }
        } else {
            assert(false);
            continue;
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
    transport::TcpTransport::Instance()->Send(msg_ptr->conn.get(), msg);
    SHARDORA_DEBUG("sync response ok des: %u, src hash64: %lu, des hash64: %lu",
        msg_ptr->header.src_sharding_id(), msg_ptr->header.hash64(), msg.hash64());
}

void KeyValueSync::ProcessSyncValueResponse(const transport::MessagePtr& msg_ptr) {
    auto& sync_msg = msg_ptr->header.sync_proto();
    assert(sync_msg.has_sync_value_res());
    auto& res_arr = sync_msg.sync_value_res().res();
    auto now_tm_us = common::TimeUtils::TimestampUs();
    SHARDORA_DEBUG("now handle kv response hash64: %lu", msg_ptr->header.hash64());
    std::map<uint32_t, std::map<uint64_t, std::shared_ptr<view_block::protobuf::ViewBlockItem>>> res_map;
    for (auto iter = res_arr.begin(); iter != res_arr.end(); ++iter) {
        std::string key = iter->key();
        if (iter->tag() == kBlockHeight) {
            key = std::to_string(iter->network_id()) + "_" +
                std::to_string(iter->pool_idx()) + "_" +
                std::to_string(iter->height());
        }

        do {
            SHARDORA_DEBUG("now handle kv response hash64: %lu, key: %s, tag: %d",
                msg_ptr->header.hash64(), 
                (iter->tag() == kBlockHeight ? key.c_str() : common::Encode::HexEncode(key).c_str()), 
                iter->tag());
            auto pb_vblock = std::make_shared<view_block::protobuf::ViewBlockItem>();
            if (!pb_vblock->ParseFromString(iter->value())) {
                SHARDORA_ERROR("pb vblock parse failed");
                assert(false);
                break;
            }
    
            if (!pb_vblock->has_qc() || pb_vblock->qc().sign_x().empty()) {
                SHARDORA_ERROR("pb vblock has no qc");
                assert(false);
                break;
            }
         
            assert(!pb_vblock->qc().sign_x().empty());
            SHARDORA_DEBUG("0 success handle network new view block: %u_%u_%lu, height: %lu key: %s, is broadcast: %d", 
                pb_vblock->qc().network_id(),
                pb_vblock->qc().pool_index(),
                pb_vblock->qc().view(),
                pb_vblock->block_info().height(),
                (iter->tag() == kBlockHeight ? key.c_str() : common::Encode::HexEncode(key).c_str()),
                iter->key().empty());
            res_map[pb_vblock->qc().network_id()][pb_vblock->qc().view()] = pb_vblock;
        } while (0);

        responsed_keys_.add(key);
        synced_map_.erase(key);
        SHARDORA_DEBUG("block response coming: %s, sync map size: %u, hash64: %lu",
            key.c_str(), synced_map_.size(), msg_ptr->header.hash64());
    }

    for (auto iter = res_map.begin(); iter != res_map.end(); ++iter) {
        auto network_id = iter->first;
        for (auto iter2 = iter->second.begin(); iter2 != iter->second.end(); ++iter2) {
            auto pb_vblock = iter2->second;
            auto thread_idx = transport::TcpTransport::Instance()->GetThreadIndexWithPool(
                pb_vblock->qc().pool_index());
            if (!network::IsSameShardOrSameWaitingPool(
                    network::kRootCongressNetworkId, 
                    network_id) && !network::IsSameToLocalShard(network_id)) {
                thread_idx = transport::TcpTransport::Instance()->GetThreadIndexWithPool(network_id);
            }
            
            vblock_queues_[thread_idx].push(pb_vblock);
            SHARDORA_DEBUG("1 success handle network new view block: %u_%u_%lu, height: %lu ", 
                pb_vblock->qc().network_id(),
                pb_vblock->qc().pool_index(),
                pb_vblock->qc().view(),
                pb_vblock->block_info().height());
        }
    }
}

}  // namespace sync

}  // namespace shardora
