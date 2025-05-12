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
    destroy_ = true;
    if (check_timer_thread_) {
        check_timer_thread_->join();
    }
}

void KeyValueSync::Init(
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<consensus::HotstuffManager>& hotstuff_mgr,
        const std::shared_ptr<db::Db>& db,
        ViewBlockSyncedCallback view_block_synced_callback) {
    block_mgr_ = block_mgr;
    hotstuff_mgr_ = hotstuff_mgr;
    db_ = db;
    view_block_synced_callback_ = view_block_synced_callback;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    network::Route::Instance()->RegisterMessage(
        common::kSyncMessage,
        std::bind(&KeyValueSync::HandleMessage, this, std::placeholders::_1));
    // check_timer_thread_ = std::make_shared<std::thread>(
    //     std::bind(&KeyValueSync::ConsensusTimerMessageThread, this));
    kv_tick_.CutOff(
        10000lu,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
}

int KeyValueSync::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void KeyValueSync::AddSyncHeight(
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority) {
    assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(network_id, pool_idx, height, priority);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    item_queues_[thread_idx].push(item);
    ZJC_DEBUG("block height add new sync item key: %s, priority: %u",
        item->key.c_str(), item->priority);
}

void KeyValueSync::AddSyncViewHash(
        uint32_t network_id, 
        uint32_t pool_idx,
        const std::string& view_hash, 
        uint32_t priority) {
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
    ZJC_DEBUG("block height add new sync item key: %s, priority: %u, item size: %u",
        common::Encode::HexEncode(item->key).c_str(), 
        item->priority, 
        item_queues_[thread_idx].size());
}

void KeyValueSync::ConsensusTimerMessageThread() {
    // while (!destroy_) {
    //     if (!common::GlobalInfo::Instance()->main_inited_success()) {
    //         usleep(10000lu);
    //         continue;
    //     }

    //     auto count = ConsensusTimerMessage();
    //     if (count < kEachTimerHandleCount) {
    //         std::unique_lock<std::mutex> lock(wait_mutex_);
    //         wait_con_.wait_for(lock, std::chrono::milliseconds(10));
    //     }
    // }
}

void KeyValueSync::ConsensusTimerMessage() {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    auto count = PopKvMessage();
    auto now_tm_ms1 = common::TimeUtils::TimestampMs();
    PopItems();
    auto now_tm_ms2 = common::TimeUtils::TimestampMs();
    CheckSyncItem();
    if (prev_sync_tmout_us_ + kSyncTimeoutPeriodUs < now_tm_us) {
        prev_sync_tmout_us_ = now_tm_us;
        CheckSyncTimeout();
    }

    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        hotstuff_mgr_->chain(i)->GetViewBlockWithHash("");
        hotstuff_mgr_->chain(i)->GetViewBlockWithHeight(0, 0);
    }

    auto now_tm_ms3 = common::TimeUtils::TimestampMs();
    auto etime = common::TimeUtils::TimestampMs();
    if (etime - now_tm_ms >= 1000000lu) {
        ZJC_DEBUG("KeyValueSync handle message use time: %lu, "
            "PopKvMessage: %lu, PopItems: %lu, CheckSyncItem: %lu", 
            (etime - now_tm_ms), 
            (now_tm_ms1 - now_tm_ms),
            (now_tm_ms2 - now_tm_ms1),
            (now_tm_ms3 - now_tm_ms2));
        assert(false);
    }

    kv_tick_.CutOff(
        10000lu,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
    // return count;
}

void KeyValueSync::PopItems() {
    for (uint8_t thread_idx = 0; thread_idx < common::kMaxThreadCount; ++thread_idx) {
        while (true) {
            SyncItemPtr item = nullptr;
            item_queues_[thread_idx].pop(&item);
            if (item == nullptr) {
                break;
            }
            
            auto iter = added_key_set_.find(item->key);
            if (iter != added_key_set_.end()) {
                ZJC_DEBUG("key exists add new sync item key: %s, priority: %u",
                    item->key.c_str(), item->priority);
                continue;
            }

            added_key_set_.insert(item->key);
            assert(added_key_set_.size() < kCacheSyncKeyValueCount);
            prio_sync_queue_[item->priority].push(item);
            CHECK_MEMORY_SIZE(prio_sync_queue_[item->priority]);
            ZJC_DEBUG("add new sync item key: %s, priority: %u",
                item->key.c_str(), item->priority);
        }
    }
}

void KeyValueSync::CheckSyncItem() {
    std::set<uint64_t> sended_neigbors;
    std::map<uint32_t, sync::protobuf::SyncMessage> sync_dht_map;
    std::set<std::string> added_key;
    bool stop = false;
    auto now_tm = common::TimeUtils::TimestampUs();
    for (int32_t i = kSyncHighest; i >= kSyncPriLowest; --i) {
        while (!prio_sync_queue_[i].empty()) {
            SyncItemPtr item = prio_sync_queue_[i].front();
            prio_sync_queue_[i].pop();
            CHECK_MEMORY_SIZE(prio_sync_queue_[i]);
            if (synced_map_.find(item->key) != synced_map_.end()) {
                continue;
            }

            if (synced_keys_.find(item->key) != synced_keys_.end()) {
                continue;
            }

            auto iter = sync_dht_map.find(item->network_id);
            if (iter == sync_dht_map.end()) {
                sync_dht_map[item->network_id] = sync::protobuf::SyncMessage();
            }

            if (added_key.find(item->key) != added_key.end()) {
                continue;
            }

            added_key.insert(item->key);
            auto sync_req = sync_dht_map[item->network_id].mutable_sync_value_req();
            sync_req->set_network_id(item->network_id);
            if (item->height != common::kInvalidUint64) {
                auto height_item = sync_req->add_heights();
                height_item->set_pool_idx(item->pool_idx);
                height_item->set_height(item->height);
                height_item->set_tag(item->tag);
                // ZJC_DEBUG("try to sync normal block: %u_%u_%lu, tag: %d",
                //     item->network_id, item->pool_idx, item->height, item->tag);
            } else {
                sync_req->add_keys(item->key);
                ZJC_DEBUG("success add to sync key: %s", 
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
            synced_map_.insert(std::make_pair(item->key, item));
            CHECK_MEMORY_SIZE(synced_map_);
            item->sync_tm_us = now_tm;
            if (synced_map_.size() > kSyncMaxKeyCount) {
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
            ZJC_ERROR("network id[%d] not exists.", network_id);
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
    ZJC_DEBUG("sync new from %s:%d, hash64: %lu, key size: %u, height size: %u",
        node->public_ip.c_str(), node->public_port, msg.hash64(),
        sync_msg.sync_value_req().keys_size(),
        sync_msg.sync_value_req().heights_size());
    return node->id_hash;
}

void KeyValueSync::HandleMessage(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& header = msg_ptr->header;
    assert(header.type() == common::kSyncMessage);
//     ZJC_DEBUG("key value sync message coming req: %d, res: %d",
//         header.sync_proto().has_sync_value_req(),
//         header.sync_proto().has_sync_value_res());
    kv_msg_queue_.push(msg_ptr);
    ZJC_DEBUG("queue size kv_msg_queue_: %d, hash: %lu",
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
    ZJC_DEBUG("handle sync value request hash: %lu, key size: %u, height size: %u", 
        msg_ptr->header.hash64(), 
        sync_msg.sync_value_req().keys_size(),
        sync_msg.sync_value_req().heights_size());
    defer({
        ZJC_DEBUG("over handle sync value request hash: %lu, key size: %u, height size: %u", 
            msg_ptr->header.hash64(), 
            sync_msg.sync_value_req().keys_size(),
            sync_msg.sync_value_req().heights_size());
    });
    for (int32_t i = 0; i < sync_msg.sync_value_req().keys_size(); ++i) {
        const std::string& key = sync_msg.sync_value_req().keys(i);
        ZJC_DEBUG("now handle sync view bock hash key: %s", 
            common::Encode::HexEncode(key).c_str());
        if (key.size() != 34) {
            continue;
        }

        uint16_t* pool_index_arr = (uint16_t*)key.c_str();
        auto view_block_ptr = hotstuff_mgr_->chain(pool_index_arr[0])->GetViewBlockWithHash(
            std::string(key.c_str() + 2, 32));
        if (view_block_ptr != nullptr && !view_block_ptr->qc().sign_x().empty()) {
            ZJC_DEBUG("success get view block request coming: %u_%u view block hash: %s, hash: %lu",
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
            ZJC_DEBUG("handle sync value view add add_size: %u request hash: %lu, "
                "net: %u, pool: %u, height: %lu",
                add_size,
                msg_ptr->header.hash64(),
                res->network_id(),
                res->pool_idx(),
                res->height());
            if (add_size >= kSyncPacketMaxSize) {
                ZJC_DEBUG("handle sync value view add_size failed request hash: %lu, "
                    "net: %u, pool: %u, height: %lu",
                    res->network_id(),
                    res->pool_idx(),
                    res->height(),
                    msg_ptr->header.hash64());
                break;
            }
        } else {
            ZJC_DEBUG("failed get view block request coming: %u_%u view block hash: %s, hash: %lu",
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
                ZJC_DEBUG("sync key value %u_%u_%lu, handle sync value failed request "
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
                ZJC_DEBUG("empty sign sync key value %u_%u_%lu, handle sync value failed request "
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
                ZJC_DEBUG("handle sync value add_size failed request hash: %lu, "
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
    ZJC_DEBUG("sync response ok des: %u, src hash64: %lu, des hash64: %lu",
        msg_ptr->header.src_sharding_id(), msg_ptr->header.hash64(), msg.hash64());
}

void KeyValueSync::ProcessSyncValueResponse(const transport::MessagePtr& msg_ptr) {
    auto& sync_msg = msg_ptr->header.sync_proto();
    assert(sync_msg.has_sync_value_res());
    auto& res_arr = sync_msg.sync_value_res().res();
    auto now_tm_us = common::TimeUtils::TimestampUs();
    ZJC_DEBUG("now handle kv response hash64: %lu", msg_ptr->header.hash64());
    for (auto iter = res_arr.begin(); iter != res_arr.end(); ++iter) {
        std::string key = iter->key();
        if (iter->tag() == kBlockHeight) {
            key = std::to_string(iter->network_id()) + "_" +
                std::to_string(iter->pool_idx()) + "_" +
                std::to_string(iter->height());
        }

        do {
            ZJC_DEBUG("now handle kv response hash64: %lu, key: %s, tag: %d",
                msg_ptr->header.hash64(), 
                (iter->tag() == kBlockHeight ? key.c_str() : common::Encode::HexEncode(key).c_str()), 
                iter->tag());
            auto pb_vblock = std::make_shared<view_block::protobuf::ViewBlockItem>();
            if (!pb_vblock->ParseFromString(iter->value())) {
                ZJC_ERROR("pb vblock parse failed");
                assert(false);
                break;
            }
    
            if (!pb_vblock->has_qc() || pb_vblock->qc().sign_x().empty()) {
                ZJC_ERROR("pb vblock has no qc");
                assert(false);
                break;
            }
    
            // if (!view_block_synced_callback_) {
            //     ZJC_ERROR("no view block synced callback inited");
            //     assert(false);
            //     break;
            // }
    
            // int res = view_block_synced_callback_(*pb_vblock);
            // ZJC_DEBUG("now handle kv response hash64: %lu, key: %s, tag: %d, sign x: %s, res: %d",
            //     msg_ptr->header.hash64(), 
            //     (iter->tag() == kBlockHeight ? key.c_str() : common::Encode::HexEncode(key).c_str()), 
            //     iter->tag(),
            //     pb_vblock->qc().sign_x().c_str(),
            //     res);
            assert(!pb_vblock->qc().sign_x().empty());
            // if (res == 1) {
            //     assert(false);
            //     break;
            // }
                
            // if (res == 0) {
                ZJC_DEBUG("0 success handle network new view block: %u_%u_%lu, height: %lu key: %s", 
                    pb_vblock->qc().network_id(),
                    pb_vblock->qc().pool_index(),
                    pb_vblock->qc().view(),
                    pb_vblock->block_info().height(),
                    (iter->tag() == kBlockHeight ? key.c_str() : common::Encode::HexEncode(key).c_str()));
                auto thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[pb_vblock->qc().pool_index()];
                vblock_queues_[thread_idx].push(pb_vblock);
            // }  
        } while (0);

        auto tmp_iter = synced_map_.find(key);
        if (tmp_iter != synced_map_.end()) {
            tmp_iter->second->responsed_timeout_us = now_tm_us + kSyncTimeoutPeriodUs;
        }

        synced_keys_.insert(key);
        timeout_queue_.push_back(key);
        if (timeout_queue_.size() >= kCacheSyncKeyValueCount) {
            synced_keys_.erase(timeout_queue_.front());
            timeout_queue_.pop_front();
        }

        assert(synced_keys_.size() < kCacheSyncKeyValueCount);
        ZJC_DEBUG("block response coming: %s, sync map size: %u, hash64: %lu",
            key.c_str(), synced_map_.size(), msg_ptr->header.hash64());
    }
}

void KeyValueSync::CheckSyncTimeout() {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    for (auto iter = synced_map_.begin(); iter != synced_map_.end();) {
        if (iter->second->sync_times >= kSyncMaxRetryTimes ||
                iter->second->responsed_timeout_us <= now_tm_us) {
            ZJC_DEBUG("remove sync key: %s, sync times: %d, "
                "responsed_timeout_us: %lu, now_tm_us: %lu",
                iter->second->key.c_str(), 
                iter->second->sync_times, 
                iter->second->responsed_timeout_us, 
                now_tm_us);
            added_key_set_.erase(iter->second->key);
            iter = synced_map_.erase(iter);
            CHECK_MEMORY_SIZE(synced_map_);
            continue;
        }

        if (iter->second->sync_tm_us + 1000000 >= now_tm_us) {
            ++iter;
            continue;
        }

        // ZJC_DEBUG("remove sync key and retry: %s, sync times: %d, "
        //     "responsed_timeout_us: %lu, now_tm_us: %lu",
        //     iter->second->key.c_str(), 
        //     iter->second->sync_times, 
        //     iter->second->responsed_timeout_us, 
        //     now_tm_us);
        added_key_set_.erase(iter->second->key);
        prio_sync_queue_[iter->second->priority].push(iter->second);
        CHECK_MEMORY_SIZE(prio_sync_queue_[iter->second->priority]);
        iter = synced_map_.erase(iter);
        CHECK_MEMORY_SIZE(synced_map_);
    }
}

}  // namespace sync

}  // namespace shardora
