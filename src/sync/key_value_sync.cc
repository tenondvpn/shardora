#include "stdafx.h"
#include "sync/key_value_sync.h"

#include "block/block_manager.h"
#include "common/global_info.h"
#include "db/db.h"
#include "dht/base_dht.h"
#include "dht/dht_function.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "network/universal_manager.h"
#include "protos/block.pb.h"
#include "sync/sync_utils.h"
#include "transport/processor.h"

namespace zjchain {

namespace sync {

KeyValueSync::KeyValueSync() {}

KeyValueSync::~KeyValueSync() {}

void KeyValueSync::AddSync(
        uint8_t thread_idx,
        uint32_t network_id,
        const std::string& key,
        uint32_t priority) {
    assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(network_id, key, priority);
    item_queues_[thread_idx].push(item);
//     ZJC_DEBUG("key value add new sync item key: %s, priority: %u",
//         item->key.c_str(), item->priority);

}

void KeyValueSync::AddSyncHeight(
        uint8_t thread_idx,
        uint32_t network_id,
        uint32_t pool_idx,
        uint64_t height,
        uint32_t priority) {
    assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(network_id, pool_idx, height, priority);
    item_queues_[thread_idx].push(item);
    ZJC_DEBUG("block height add new sync item key: %s, priority: %u",
        item->key.c_str(), item->priority);
}

void KeyValueSync::ConsensusTimerMessage(const transport::MessagePtr& msg_ptr) {
    ZJC_INFO("KeyValueSync timer coming.");
    PopItems();
    ZJC_INFO("KeyValueSync timer coming 0.");
    auto now_tm_us = common::TimeUtils::TimestampUs();
    CheckSyncItem(msg_ptr->thread_idx);
    ZJC_INFO("KeyValueSync timer coming 1.");
    if (prev_sync_tmout_us_ + kSyncTimeoutPeriodUs < now_tm_us) {
        prev_sync_tmout_us_ = now_tm_us;
        CheckSyncTimeout();
    }
    ZJC_INFO("KeyValueSync timer over.");
}

void KeyValueSync::PopItems() {
    uint32_t pop_count = 0;
    for (uint8_t thread_idx = 0; thread_idx < common::kMaxThreadCount; ++thread_idx) {
        while (item_queues_[thread_idx].size() > 0 && pop_count++ < 64) {
            SyncItemPtr item = nullptr;
            item_queues_[thread_idx].pop(&item);
            auto iter = added_key_set_.find(item->key);
            if (iter != added_key_set_.end()) {
                ZJC_DEBUG("key exists add new sync item key: %s, priority: %u",
                    item->key.c_str(), item->priority);
                continue;
            }

            added_key_set_.insert(item->key);
            auto tmp_iter = synced_map_.find(item->key);
            if (tmp_iter != synced_map_.end()) {
                ZJC_DEBUG("key synced add new sync item key: %s, priority: %u",
                    item->key.c_str(), item->priority);
                continue;
            }

            prio_sync_queue_[item->priority].push(item);
//             ZJC_DEBUG("add new sync item key: %s, priority: %u",
//                 item->key.c_str(), item->priority);
        }
    }
}

void KeyValueSync::Init(
        block::BlockAggValidCallback block_agg_valid_func,
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<db::Db>& db) {
    block_agg_valid_func_ = block_agg_valid_func;
    block_mgr_ = block_mgr;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    network::Route::Instance()->RegisterMessage(
        common::kSyncMessage,
        std::bind(&KeyValueSync::HandleMessage, this, std::placeholders::_1));
    transport::Processor::Instance()->RegisterProcessor(
        common::kPoolTimerMessage,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this, std::placeholders::_1));
}

void KeyValueSync::CheckSyncItem(uint8_t thread_idx) {
    std::set<uint64_t> sended_neigbors;
    std::map<uint32_t, sync::protobuf::SyncMessage> sync_dht_map;
    std::set<std::string> added_key;
    bool stop = false;
    auto now_tm = common::TimeUtils::TimestampUs();
    for (int32_t i = kSyncHighest; i >= kSyncPriLowest; --i) {
        while (!prio_sync_queue_[i].empty()) {
            SyncItemPtr item = prio_sync_queue_[i].front();
            prio_sync_queue_[i].pop();
            if (synced_map_.find(item->key) != synced_map_.end()) {
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
            } else {
                sync_req->add_keys(item->key);
            }

            if (sync_req->keys_size() + sync_req->heights_size() > (int32_t)kEachRequestMaxSyncKeyCount) {
                uint64_t choose_node = SendSyncRequest(
                    thread_idx,
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
            synced_map_.insert(std::make_pair(item->key, item));
            item->sync_tm_us = now_tm;
            if (synced_map_.size() > kSyncMaxKeyCount) {
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
                thread_idx,
                iter->first,
                iter->second,
                sended_neigbors);
            if (choose_node != 0) {
                sended_neigbors.insert(choose_node);
            }
        }
    }
}

uint64_t KeyValueSync::SendSyncRequest(
        uint8_t thread_idx,
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
        ZJC_ERROR("network id[%d] not exists.", network_id);
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
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(network_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kSyncMessage);
    *msg.mutable_sync_proto() = sync_msg;
    transport::TcpTransport::Instance()->Send(
        thread_idx, node->public_ip, node->public_port, msg);
    ZJC_DEBUG("sync new from %s:%d", node->public_ip.c_str(), node->public_port);
    return node->id_hash;
}

void KeyValueSync::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    assert(header.type() == common::kSyncMessage);
//     ZJC_DEBUG("key value sync message coming req: %d, res: %d",
//         header.sync_proto().has_sync_value_req(),
//         header.sync_proto().has_sync_value_res());
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
    for (int32_t i = 0; i < sync_msg.sync_value_req().keys_size(); ++i) {
        const std::string& key = sync_msg.sync_value_req().keys(i);
        std::string value;
        if (db_->Get(key, &value).ok()) {
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
        auto network_id = sync_msg.sync_value_req().network_id();
        block::protobuf::Block block;
        if (!prefix_db_->GetBlockWithHeight(
                network_id,
                sync_msg.sync_value_req().heights(i).pool_idx(),
                sync_msg.sync_value_req().heights(i).height(),
                &block)) {
            continue;
        }


        if (!AddSyncKeyValue(&msg, block)) {
            continue;
        }

        auto res = sync_res->add_res();
        res->set_network_id(network_id);
        res->set_pool_idx(sync_msg.sync_value_req().heights(i).pool_idx());
        res->set_height(sync_msg.sync_value_req().heights(i).height());
        res->set_value(block.SerializeAsString());
        add_size += 16 + res->value().size();
        if (add_size >= kSyncPacketMaxSize) {
            break;
        }
    }

    if (add_size == 0) {
        return;
    }

    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(msg_ptr->header.src_sharding_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kSyncMessage);
    transport::TcpTransport::Instance()->Send(msg_ptr->thread_idx, msg_ptr->conn, msg);
    ZJC_DEBUG("sync response ok des: %u", msg_ptr->header.src_sharding_id());
}

bool KeyValueSync::AddSyncKeyValue(transport::protobuf::Header* msg, const block::protobuf::Block& block) {
    auto* sync_info = msg->mutable_sync();
    for (int32_t i = 0; i < block.tx_list_size(); ++i) {
        auto& tx = block.tx_list(i);
        for (int32_t j = 0; j < block.tx_list(i).storages_size(); ++j) {
            auto& storage = block.tx_list(i).storages(j);
//             ZJC_DEBUG("add storage %s, %s, %d", storage.key().c_str(), common::Encode::HexEncode(storage.val_hash()).c_str(), storage.val_size());
            if (storage.val_hash().size() == 32) {
                std::string val;
                if (!prefix_db_->GetTemporaryKv(storage.val_hash(), &val)) {
                    continue;
                }

                auto* sync_item = sync_info->add_items();
                sync_item->set_key(storage.val_hash());
                sync_item->set_value(val);
            }
        }
    }

    return true;
}

void KeyValueSync::ProcessSyncValueResponse(const transport::MessagePtr& msg_ptr) {
    auto& sync_msg = msg_ptr->header.sync_proto();
    assert(sync_msg.has_sync_value_res());
    auto& res_arr = sync_msg.sync_value_res().res();
    for (auto iter = res_arr.begin(); iter != res_arr.end(); ++iter) {
        std::string key = iter->key();
        if (iter->has_height()) {
            key = std::to_string(iter->network_id()) + "_" +
                std::to_string(iter->pool_idx()) + "_" +
                std::to_string(iter->height());
            auto block_item = std::make_shared<block::protobuf::Block>();
            if (block_item->ParseFromString(iter->value())) {
                if (block_item->network_id() != common::GlobalInfo::Instance()->network_id() &&
                        block_item->network_id() + network::kConsensusWaitingShardOffset !=
                        common::GlobalInfo::Instance()->network_id()) {
                    if (block_agg_valid_func_(*block_item)) {
                        block_mgr_->NetworkNewBlock(msg_ptr->thread_idx, block_item);
                    } else {
                        ZJC_ERROR("failed check agg sign sync block message net: %u, pool: %u, height: %lu, block hash: %s",
                            block_item->network_id(),
                            block_item->pool_index(),
                            block_item->height(),
                            common::Encode::HexEncode(block_item->hash()).c_str());
                        //assert(false);
                    }
                }
            }
        }

        auto tmp_iter = synced_map_.find(key);
        if (tmp_iter != synced_map_.end()) {
            added_key_set_.erase(tmp_iter->second->key);
            synced_map_.erase(tmp_iter);
        } else {
//             assert(false);
        }

        ZJC_DEBUG("block response coming: %s, sync map size: %u",
            key.c_str(), synced_map_.size());
    }
}

void KeyValueSync::CheckSyncTimeout() {
    auto now_tm = common::TimeUtils::TimestampUs();
    for (auto iter = synced_map_.begin(); iter != synced_map_.end();) {
        if (iter->second->sync_times >= kSyncMaxRetryTimes) {
            added_key_set_.erase(iter->second->key);
            synced_map_.erase(iter++);
            continue;
        }

        if (iter->second->sync_tm_us + 500000 >= now_tm) {
            ++iter;
            continue;
        }

        prio_sync_queue_[iter->second->priority].push(iter->second);
        synced_map_.erase(iter++);
    }
}

}  // namespace sync

}  // namespace zjchain
