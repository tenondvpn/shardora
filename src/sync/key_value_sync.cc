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
#include <common/log.h>

namespace shardora {

namespace sync {

KeyValueSync::KeyValueSync() {}

KeyValueSync::~KeyValueSync() {}

void KeyValueSync::AddSync(
        uint32_t network_id,
        const std::string& key,
        uint32_t priority) {
    assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(network_id, key, priority);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    item_queues_[thread_idx].push(item);
    ZJC_DEBUG("queue size thread_idx: %d, item_queues_: %d", thread_idx, item_queues_[thread_idx].size());
//     ZJC_DEBUG("key value add new sync item key: %s, priority: %u",
//         item->key.c_str(), item->priority);

}

void KeyValueSync::Init(
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<db::Db>& db) {
    block_mgr_ = block_mgr;
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    network::Route::Instance()->RegisterMessage(
        common::kSyncMessage,
        std::bind(&KeyValueSync::HandleMessage, this, std::placeholders::_1));
    tick_.CutOff(
        100000lu,
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

void KeyValueSync::AddSyncElectBlock(
        uint32_t network_id,
        uint32_t elect_network_id,
        uint64_t height,
        uint32_t priority) {
    assert(priority <= kSyncHighest);
    auto item = std::make_shared<SyncItem>(network_id, elect_network_id, height, priority, kElectBlock);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    item_queues_[thread_idx].push(item);
    ZJC_DEBUG("block height add new sync item key: %s, priority: %u",
        item->key.c_str(), item->priority);
}

void KeyValueSync::ConsensusTimerMessage() {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    PopKvMessage();
    PopItems();
    CheckSyncItem();
    if (prev_sync_tmout_us_ + kSyncTimeoutPeriodUs < now_tm_us) {
        prev_sync_tmout_us_ = now_tm_us;
        CheckSyncTimeout();
    }

    auto etime = common::TimeUtils::TimestampMs();
    if (etime - now_tm_ms >= 10) {
        ZJC_DEBUG("KeyValueSync handle message use time: %lu", (etime - now_tm_ms));
    }

    tick_.CutOff(
        100000lu,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
}

void KeyValueSync::PopItems() {
    uint32_t pop_count = 0;
    for (uint8_t thread_idx = 0; thread_idx < common::kMaxThreadCount; ++thread_idx) {
        while (pop_count++ < 64) {
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
            auto tmp_iter = synced_map_.find(item->key);
            if (tmp_iter != synced_map_.end()) {
                ZJC_DEBUG("key synced add new sync item key: %s, priority: %u",
                    item->key.c_str(), item->priority);
                continue;
            }

            prio_sync_queue_[item->priority].push(item);
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
                if (item->tag == kElectBlock) {
                    ZJC_DEBUG("try to sync elect block: %u_%u_%lu", item->network_id, item->pool_idx, item->height);
                } else {
                    ZJC_DEBUG("try to sync normal block: %u_%u_%lu", item->network_id, item->pool_idx, item->height);
                }
            } else {
                sync_req->add_keys(item->key);
            }

            if (sync_req->keys_size() + sync_req->heights_size() > (int32_t)kEachRequestMaxSyncKeyCount) {
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
    transport::TcpTransport::Instance()->Send(node->public_ip, node->public_port, msg);
    ZJC_DEBUG("sync new from %s:%d", node->public_ip.c_str(), node->public_port);
    return node->id_hash;
}

void KeyValueSync::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    assert(header.type() == common::kSyncMessage);
//     ZJC_DEBUG("key value sync message coming req: %d, res: %d",
//         header.sync_proto().has_sync_value_req(),
//         header.sync_proto().has_sync_value_res());
    kv_msg_queue_.push(msg_ptr);
    ZJC_DEBUG("queue size kv_msg_queue_: %d, hash: %lu",
        kv_msg_queue_.size(), msg_ptr->header.hash64());
    
}

void KeyValueSync::PopKvMessage() {
    while (true) {
        transport::MessagePtr msg_ptr = nullptr;
        if (!kv_msg_queue_.pop(&msg_ptr) || msg_ptr == nullptr) {
            break;
        }

        HandleKvMessage(msg_ptr);
    }
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
    ZJC_DEBUG("handle sync value request hash: %lu", msg_ptr->header.hash64());
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

    auto network_id = sync_msg.sync_value_req().network_id();
    for (int32_t i = 0; i < sync_msg.sync_value_req().heights_size(); ++i) {
        if (sync_msg.sync_value_req().heights(i).tag() == kBlockHeight) {
            block::protobuf::Block block;
            if (!prefix_db_->GetBlockWithHeight(
                    network_id,
                    sync_msg.sync_value_req().heights(i).pool_idx(),
                    sync_msg.sync_value_req().heights(i).height(),
                    &block)) {
                ZJC_DEBUG("handle sync value failed request hash: %lu, "
                    "net: %u, pool: %u, height: %lu",
                    network_id, 
                    sync_msg.sync_value_req().heights(i).pool_idx(),
                    sync_msg.sync_value_req().heights(i).height(),
                    msg_ptr->header.hash64());
                continue;
            }

            auto res = sync_res->add_res();
            res->set_network_id(network_id);
            res->set_pool_idx(sync_msg.sync_value_req().heights(i).pool_idx());
            res->set_height(sync_msg.sync_value_req().heights(i).height());
            res->set_value(block.SerializeAsString());
            add_size += 16 + res->value().size();
            if (add_size >= kSyncPacketMaxSize) {
                ZJC_DEBUG("handle sync value add_size failed request hash: %lu, "
                    "net: %u, pool: %u, height: %lu",
                    network_id,
                    sync_msg.sync_value_req().heights(i).pool_idx(),
                    sync_msg.sync_value_req().heights(i).height(),
                    msg_ptr->header.hash64());
                break;
            }
        } else if (sync_msg.sync_value_req().heights(i).tag() == kElectBlock) {
            ResponseElectBlock(network_id, sync_msg.sync_value_req().heights(i), msg, sync_res, add_size);
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
    ZJC_DEBUG("sync response ok des: %u, hash64: %lu", msg_ptr->header.src_sharding_id(), msg.hash64());
}

void KeyValueSync::ResponseElectBlock(
        uint32_t network_id,
        const sync::protobuf::SyncHeightItem& sync_item,
        transport::protobuf::Header& msg,
        sync::protobuf::SyncValueResponse* sync_res,
        uint32_t& add_size) {
    ZJC_DEBUG("request elect block coming.");
    if (network_id >= network::kConsensusShardEndNetworkId ||
            network_id < network::kRootCongressNetworkId) {
        ZJC_DEBUG("request elect block coming network invalid: %u", network_id);
        return;
    }

    auto elect_network_id = sync_item.pool_idx();
    auto& shard_set = shard_with_elect_height_[elect_network_id];
    auto iter = shard_set.rbegin();
    std::vector<uint64_t> valid_elect_heights;
    uint64_t min_height = 1;
    if (iter != shard_set.rend()) {
        min_height = *iter;
    }

    uint64_t elect_height = elect_net_heights_map_[elect_network_id];
    while (elect_height >= min_height) {
        block::protobuf::Block block;
        if (!prefix_db_->GetBlockWithHeight(
                network::kRootCongressNetworkId,
                elect_network_id % common::kImmutablePoolSize,
                elect_height,
                &block)) {
            ZJC_DEBUG("block invalid network: %u, pool: %lu, height: %lu",
                network::kRootCongressNetworkId, elect_network_id % common::kImmutablePoolSize, elect_height);
            return;
        }

        elect::protobuf::ElectBlock prev_elect_block;
        bool ec_block_loaded = false;
        assert(block.tx_list_size() == 1);
        for (int32_t i = 0; i < block.tx_list(0).storages_size(); ++i) {
            ZJC_DEBUG("get tx storage key: %s, tx size: %d", block.tx_list(0).storages(i).key().c_str(), block.tx_list_size());
            if (block.tx_list(0).storages(i).key() == protos::kElectNodeAttrElectBlock) {
                if (!prev_elect_block.ParseFromString(block.tx_list(0).storages(i).value())) {
                    assert(false);
                    return;
                }

                ec_block_loaded = true;
                break;
            }
        }

        if (!ec_block_loaded) {
            assert(false);
            return;
        }

        valid_elect_heights.push_back(elect_height);
        ZJC_DEBUG("success get network_id: %u, pool: %u, elect height: %lu, prev: %lu, min_height: %lu",
            network::kRootCongressNetworkId, sync_item.pool_idx(), elect_height,
            prev_elect_block.prev_members().prev_elect_height(), min_height);
        if (elect_height == prev_elect_block.prev_members().prev_elect_height()) {
            assert(false);
            return;
        }

        elect_height = prev_elect_block.prev_members().prev_elect_height();
    }

    for (auto iter = valid_elect_heights.begin(); iter != valid_elect_heights.end(); ++iter) {
        shard_set.insert(*iter);
    }

    auto fiter = shard_set.find(sync_item.height());
    if (fiter == shard_set.end()) {
        ZJC_DEBUG("find height error block invalid network: %u, pool: %lu, height: %lu",
            network::kRootCongressNetworkId, network_id % common::kImmutablePoolSize, sync_item.height());
        return;
    }

//    ++fiter;
    for (; fiter != shard_set.end(); ++fiter) {
        block::protobuf::Block block;
        if (!prefix_db_->GetBlockWithHeight(
                network::kRootCongressNetworkId,
                elect_network_id % common::kImmutablePoolSize,
                *fiter,
                &block)) {
            ZJC_DEBUG("block invalid network: %u, pool: %lu, height: %lu",
                network::kRootCongressNetworkId, elect_network_id % common::kImmutablePoolSize, *fiter);
            return;
        }

        auto res = sync_res->add_res();
        res->set_network_id(block.network_id());
        res->set_pool_idx(block.pool_index());
        res->set_height(block.height());
        res->set_value(block.SerializeAsString());
        res->set_tag(kElectBlock); // no use
        add_size += 16 + res->value().size();
        ZJC_DEBUG("block success network: %u, pool: %lu, height: %lu, add_size: %u, kSyncPacketMaxSize: %u",
            block.network_id(), block.pool_index(), block.height(), add_size, kSyncPacketMaxSize);
        if (add_size >= kSyncPacketMaxSize) {
            break;
        }
    }
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
                // 对于选举块同步来说，创世选举块不需要验签
                bool need_valid = (block_item->electblock_height() != 1);
                // 针对 root 网络的选举块同步
                if (block_item->network_id() != common::GlobalInfo::Instance()->network_id() &&
                        block_item->network_id() + network::kConsensusWaitingShardOffset !=
                        common::GlobalInfo::Instance()->network_id()) {
                    // TODO 暂时屏蔽创世选举块的验签，后续通过消息体中的 commom pk 验证
                    ZJC_DEBUG("sync elect block, elect height: %u, height: %u, key: %s, block hash: %s", 
                        block_item->electblock_height(), block_item->height(), 
                        key.c_str(), common::Encode::HexEncode(block_item->hash()).c_str());
                    block_mgr_->NetworkNewBlock(block_item, need_valid);
                } else { // TODO 本网络的就不用同步吗？
                    block_mgr_->NetworkNewBlock(block_item, need_valid);
                    ZJC_DEBUG("sync normal block, elect height: %u, height: %u, network_id: %u, "
                        "pool: %u, key: %s, block hash: %s",
                        block_item->electblock_height(),
                        block_item->height(),
                        block_item->network_id(),
                        block_item->pool_index(),
                        key.c_str(), 
                        common::Encode::HexEncode(block_item->hash()).c_str());
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

        // synced_keys_.insert(key);
        timeout_queue_.push_back(key);
        if (timeout_queue_.size() >= 10240) {
            synced_keys_.erase(timeout_queue_.front());
            timeout_queue_.pop_front();
        }

        ZJC_DEBUG("block response coming: %s, sync map size: %u, hash64: %lu",
            key.c_str(), synced_map_.size(), msg_ptr->header.hash64());
    }
}

void KeyValueSync::CheckSyncTimeout() {
    
    auto now_tm = common::TimeUtils::TimestampUs();
    for (auto iter = synced_map_.begin(); iter != synced_map_.end();) {
        if (iter->second->sync_times >= kSyncMaxRetryTimes) {
            added_key_set_.erase(iter->second->key);
            iter = synced_map_.erase(iter);
            continue;
        }

        if (iter->second->sync_tm_us + 500000 >= now_tm) {
            ++iter;
            continue;
        }

        added_key_set_.erase(iter->second->key);
        prio_sync_queue_[iter->second->priority].push(iter->second);
        iter = synced_map_.erase(iter);
    }
}

}  // namespace sync

}  // namespace shardora
