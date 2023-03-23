#include "stdafx.h"
#include "sync/key_value_sync.h"

#include "bft/proto/bft.pb.h"
#include "bft/bft_manager.h"
#include "block/block_manager.h"
#include "db/db.h"
#include "dht/base_dht.h"
#include "dht/dht_function.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "network/universal_manager.h"
#include "sync/sync_utils.h"
#include "sync/proto/sync_proto.h"
#include "transport/proto/transport.pb.h"

namespace zjchain {

namespace sync {

KeyValueSync* KeyValueSync::Instance() {
    static KeyValueSync ins;
    return &ins;
}

KeyValueSync::KeyValueSync() {
    network::Route::Instance()->RegisterMessage(
            common::kSyncMessage,
            std::bind(&KeyValueSync::HandleMessage, this, std::placeholders::_1));
    Init();
}

KeyValueSync::~KeyValueSync() {}

int KeyValueSync::AddSync(uint32_t network_id, const std::string& key, uint32_t priority) {
    assert(priority <= kSyncHighest);
    {
        std::lock_guard<std::mutex> guard(added_key_set_mutex_);
        auto iter = added_key_set_.find(key);
        if (iter != added_key_set_.end()) {
            return kSyncKeyAdded;
        }

        added_key_set_.insert(key);
    }

    if (db::Db::Instance()->Exist(key)) {
//         SYNC_DEBUG("::Db::Instance()->Exist [%d] [%s]", network_id, common::Encode::HexEncode(key).c_str());
//         if (HandleExistsBlock(key) == kSyncSuccess) {
//             return kSyncBlockReloaded;
//         }
        return kSyncKeyExsits;
    }

    {
        std::lock_guard<std::mutex> guard(synced_map_mutex_);
        auto tmp_iter = synced_map_.find(key);
        if (tmp_iter != synced_map_.end()) {
            SYNC_ERROR("kSyncKeyAdded [%d] [%s]", network_id, common::Encode::HexEncode(key).c_str());
            return kSyncKeyAdded;
        }
    }

    auto item = std::make_shared<SyncItem>(network_id, key, priority);
    {
        std::lock_guard<std::mutex> guard(prio_sync_queue_[priority].mutex);
        prio_sync_queue_[priority].sync_queue.push(item);
    }
//     SYNC_ERROR("ttttttttttttttt new sync item [%d] [%s]", network_id, common::Encode::HexEncode(key).c_str());
    return kSyncSuccess;
}

int KeyValueSync::AddSyncHeight(uint32_t network_id, uint32_t pool_idx, uint64_t height, uint32_t priority) {
    assert(priority <= kSyncHighest);
    {
        std::string key = std::to_string(network_id) + "_" +
            std::to_string(pool_idx) + "_" +
            std::to_string(height);
        std::lock_guard<std::mutex> guard(added_key_set_mutex_);
        auto iter = added_key_set_.find(key);
        if (iter != added_key_set_.end()) {
            return kSyncKeyAdded;
        }

        added_key_set_.insert(key);
    }

    auto item = std::make_shared<SyncItem>(network_id, pool_idx, height, priority);
    {
        std::lock_guard<std::mutex> guard(prio_sync_queue_[priority].mutex);
        prio_sync_queue_[priority].sync_queue.push(item);
    }

    return kSyncSuccess;
}

void KeyValueSync::Init() {
#ifndef ZJC_UNITTEST
    tick_.CutOff(kSyncTickPeriod, std::bind(&KeyValueSync::CheckSyncItem, this));
    sync_timeout_tick_.CutOff(
            kTimeoutCheckPeriod,
            std::bind(&KeyValueSync::CheckSyncTimeout, this));
#endif
}

void KeyValueSync::Destroy() {
    tick_.Destroy();
    sync_timeout_tick_.Destroy();
}

void KeyValueSync::CheckSyncItem() {
    std::set<uint64_t> sended_neigbors;
    std::map<uint32_t, sync::protobuf::SyncMessage> sync_dht_map;
    std::set<std::string> added_key;
    bool stop = false;
    for (int32_t i = kSyncHighest; i >= kSyncPriLowest; --i) {
        std::lock_guard<std::mutex> guard(prio_sync_queue_[i].mutex);
        while (!prio_sync_queue_[i].sync_queue.empty()) {
            SyncItemPtr item = prio_sync_queue_[i].sync_queue.front();
            prio_sync_queue_[i].sync_queue.pop();
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
            } else {
                sync_req->add_keys(item->key);
            }

            if (sync_req->keys_size() + sync_req->heights_size() > (int32_t)kMaxSyncKeyCount) {
                uint64_t choose_node = SendSyncRequest(
                        item->network_id,
                        sync_dht_map[item->network_id],
                        sended_neigbors);
                if (choose_node != 0) {
                    sended_neigbors.insert(choose_node);
                }

                sync_req->clear_keys();
                sync_req->clear_heights();
                if (sended_neigbors.size() > kSyncNeighborCount) {
                    stop = true;
                    break;
                }
            }

            ++(item->sync_times);
            {
                std::lock_guard<std::mutex> tmp_guard(synced_map_mutex_);
                if (synced_map_.find(item->key) != synced_map_.end()) {
                    continue;
                }

                synced_map_.insert(std::make_pair(item->key, item));
                if (synced_map_.size() > kSyncMaxKeyCount) {
                    stop = true;
                    break;
                }
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

#ifndef ZJC_UNITTEST
    tick_.CutOff(kSyncTickPeriod, std::bind(&KeyValueSync::CheckSyncItem, this));
#endif
}

uint64_t KeyValueSync::SendSyncRequest(
        uint32_t network_id,
        const sync::protobuf::SyncMessage& sync_msg,
        const std::set<uint64_t>& sended_neigbors) {
    std::vector<dht::NodePtr> nodes;
    dht::DhtKeyManager dht_key(network_id, 0);
    auto dht = network::DhtManager::Instance()->GetDht(network_id);
    if (dht) {
        nodes = *dht->readonly_dht();
    }

    if (nodes.empty()) {
        if (network_id >= network::kConsensusShardEndNetworkId) {
            network_id -= network::kConsensusWaitingShardOffset;
            dht = network::DhtManager::Instance()->GetDht(network_id);
            if (dht) {
                nodes = *dht->readonly_dht();
            }
        }

        if (nodes.empty()) {
            dht = network::UniversalManager::Instance()->GetUniversal(network::kUniversalNetworkId);
            if (dht) {
                auto readonly_dht = dht->readonly_dht();
                nodes = dht::DhtFunction::GetClosestNodes(*readonly_dht, dht_key.StrKey(), 4);
            }
        }
    }

    if (nodes.empty()) {
        SYNC_ERROR("network id[%d] not exists.", network_id);
        return 0;
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
    dht->SetFrequently(msg);
    SyncProto::CreateSyncValueReqeust(dht->local_node(), node, sync_msg, msg);

#ifdef ZJC_UNITTEST
    test_sync_req_msg_ = msg;
    return node->id_hash;
#endif

    transport::MultiThreadHandler::Instance()->tcp_transport()->Send(
            node->public_ip(), node->local_port + 1, 0, msg);
    return node->id_hash;
}

void KeyValueSync::HandleMessage(const transport::TransportMessagePtr& header_ptr) {
    auto header = *header_ptr;
    assert(header.type() == common::kSyncMessage);
    protobuf::SyncMessage sync_msg;
    if (!sync_msg.ParseFromString(header.data())) {
        DHT_ERROR("protobuf::DhtMessage ParseFromString failed!");
        return;
    }

    if (sync_msg.has_sync_value_req()) {
        ProcessSyncValueRequest(header, sync_msg);
    }

    if (sync_msg.has_sync_value_res()) {
        ProcessSyncValueResponse(header, sync_msg);
    }
}

void KeyValueSync::ProcessSyncValueRequest(
        const transport::protobuf::Header& header,
        protobuf::SyncMessage& sync_msg) {
    assert(sync_msg.has_sync_value_req());
//     auto dht = network::DhtManager::Instance()->GetDht(
//             sync_msg.sync_value_req().network_id());
//     if (!dht) {
//         SYNC_ERROR("sync from network[%u] not exists",
//                 sync_msg.sync_value_req().network_id());
//         return;
//     }

    protobuf::SyncMessage res_sync_msg;
    auto sync_res = res_sync_msg.mutable_sync_value_res();
    uint32_t add_size = 0;
    for (int32_t i = 0; i < sync_msg.sync_value_req().keys_size(); ++i) {
        const std::string& key = sync_msg.sync_value_req().keys(i);
        std::string value;
        if (db::Db::Instance()->Get(key, &value).ok()) {
            auto res = sync_res->add_res();
            res->set_key(key);
            res->set_value(value);
            add_size += key.size() + value.size();
            if (add_size >= kSyncPacketMaxSize) {
                break;
            }
        }
    }

    for (int32_t i = 0; i < sync_msg.sync_value_req().heights_size(); ++i) {
        std::string value;
//         if (sync_msg.sync_value_req().network_id() != common::GlobalInfo::Instance()->network_id()) {
//             continue;
//         }
        auto network_id = sync_msg.sync_value_req().network_id();
        if (network_id >= network::kConsensusShardEndNetworkId) {
            network_id -= network::kConsensusWaitingShardOffset;
        }

        if (block::BlockManager::Instance()->GetBlockStringWithHeight(
                network_id,
                sync_msg.sync_value_req().heights(i).pool_idx(),
                sync_msg.sync_value_req().heights(i).height(),
                &value) != block::kBlockSuccess) {
            continue;
        }

        auto res = sync_res->add_res();
        res->set_network_id(network_id);
        res->set_pool_idx(sync_msg.sync_value_req().heights(i).pool_idx());
        res->set_height(sync_msg.sync_value_req().heights(i).height());
        res->set_value(value);
        add_size += 16 + value.size();
        if (add_size >= kSyncPacketMaxSize) {
            break;
        }
    }

    if (add_size == 0) {
        return;
    }

    transport::protobuf::Header msg;
    auto dht = network::UniversalManager::Instance()->GetUniversal(network::kUniversalNetworkId);
    dht->SetFrequently(msg);
    SyncProto::CreateSyncValueResponse(dht->local_node(), header, res_sync_msg, msg);

#ifdef ZJC_UNITTEST
    test_sync_res_msg_ = msg;
    return;
#endif

    transport::MultiThreadHandler::Instance()->Send(
        header.from_ip(), header.from_port(), 0, msg);
}

int KeyValueSync::HandleExistsBlock(const std::string& key) {
    std::string val;
    auto res = db::Db::Instance()->Get(key, &val);
    if (!res.ok()) {
        return kSyncError;
    }

    auto zjc_block = std::make_shared<bft::protobuf::Block>();
    if (zjc_block->ParseFromString(val) && zjc_block->hash() == key) {
        db::DbWriteBach db_batch;
        block::BlockManager::Instance()->AddNewBlock(zjc_block, db_batch, true, false);
        return kSyncSuccess;
    }

    return kSyncError;
}

void KeyValueSync::ProcessSyncValueResponse(
        const transport::protobuf::Header& header,
        protobuf::SyncMessage& sync_msg) {
    assert(sync_msg.has_sync_value_res());
    auto& res_arr = sync_msg.sync_value_res().res();
//     SYNC_DEBUG("recv sync response from[%s:%d] key size: %u",
//         header.from_ip().c_str(), header.from_port(), res_arr.size());
    for (auto iter = res_arr.begin(); iter != res_arr.end(); ++iter) {
        auto block_item = std::make_shared<bft::protobuf::Block>();
        if (block_item->ParseFromString(iter->value()) &&
                (iter->has_height() || block_item->hash() == iter->key())) {
            SYNC_ERROR("ttttttttttttttt recv sync response [%s], net: %d, pool_idx: %d, height: %lu",
                common::Encode::HexEncode(iter->key()).c_str(),
                block_item->network_id(),
                block_item->pool_index(),
                iter->height());
            bft::BftManager::Instance()->AddKeyValueSyncBlock(header, block_item);
        } else {
            db::Db::Instance()->Put(iter->key(), iter->value());
        }

        std::string key = iter->key();
        if (iter->has_height()) {
            key = std::to_string(iter->network_id()) + "_" +
                std::to_string(iter->pool_idx()) + "_" +
                std::to_string(iter->height());
        }

        {
            std::lock_guard<std::mutex> guard(synced_map_mutex_);
            auto tmp_iter = synced_map_.find(key);
            if (tmp_iter != synced_map_.end()) {
                {
                    std::lock_guard<std::mutex> guard(added_key_set_mutex_);
                    added_key_set_.erase(tmp_iter->second->key);
                }

                synced_map_.erase(tmp_iter);
            }
        }
    }
}

void KeyValueSync::CheckSyncTimeout() {
    {
        std::lock_guard<std::mutex> guard(synced_map_mutex_);
        for (auto iter = synced_map_.begin(); iter != synced_map_.end();) {
            if (iter->second->sync_times >= kSyncMaxRetryTimes) {
                {
                    std::lock_guard<std::mutex> guard(added_key_set_mutex_);
                    added_key_set_.erase(iter->second->key);
                }

                synced_map_.erase(iter++);
                continue;
            }

            {
                std::lock_guard<std::mutex> tmp_guard(prio_sync_queue_[iter->second->priority].mutex);
                prio_sync_queue_[iter->second->priority].sync_queue.push(iter->second);
            }

            synced_map_.erase(iter++);
        }
    }
    
#ifndef ZJC_UNITTEST
    sync_timeout_tick_.CutOff(
            kTimeoutCheckPeriod,
            std::bind(&KeyValueSync::CheckSyncTimeout, this));
#endif
}

}  // namespace sync

}  // namespace zjchain
