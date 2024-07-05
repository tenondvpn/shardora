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
#include <protos/view_block.pb.h>

namespace shardora {

namespace sync {

KeyValueSync::KeyValueSync() {}

KeyValueSync::~KeyValueSync() {}

#ifndef ENABLE_HOTSTUFF
void KeyValueSync::Init(
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<db::Db>& db,
        block::BlockAggValidCallback block_agg_valid_func) {
    block_mgr_ = block_mgr;
    db_ = db;
    block_agg_valid_func_ = block_agg_valid_func;

    prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    network::Route::Instance()->RegisterMessage(
        common::kSyncMessage,
        std::bind(&KeyValueSync::HandleMessage, this, std::placeholders::_1));
    kv_tick_.CutOff(
        100000lu,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
}
#else
void KeyValueSync::Init(
        const std::shared_ptr<block::BlockManager>& block_mgr,
        const std::shared_ptr<db::Db>& db,
        ViewBlockSyncedCallback view_block_synced_callback) {
    block_mgr_ = block_mgr;
    db_ = db;
    view_block_synced_callback_ = view_block_synced_callback;

    prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    network::Route::Instance()->RegisterMessage(
        common::kSyncMessage,
        std::bind(&KeyValueSync::HandleMessage, this, std::placeholders::_1));
    kv_tick_.CutOff(
        100000lu,
        std::bind(&KeyValueSync::ConsensusTimerMessage, this));
}
#endif
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
    ZJC_INFO("block height add new sync item key: %s, priority: %u",
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

#ifndef ENABLE_HOTSTUFF
    CheckNotCheckedBlocks();
#endif
    kv_tick_.CutOff(
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
            // auto tmp_iter = synced_map_.find(item->key);
            // if (tmp_iter != synced_map_.end()) {
            //     ZJC_DEBUG("key synced add new sync item key: %s, priority: %u",
            //         item->key.c_str(), item->priority);
            //     continue;
            // }

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
            auto& block_map = net_with_pool_blocks_[item->network_id].pool_blocks[item->pool_idx];
            auto block_iter = block_map.find(item->height);
            if (block_iter != block_map.end()) {
                continue;
            }

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
                ZJC_DEBUG("try to sync normal block: %u_%u_%lu", item->network_id, item->pool_idx, item->height);
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
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    transport::TcpTransport::Instance()->Send(node->public_ip, node->public_port, msg);
    ZJC_DEBUG("sync new from %s:%d, hash64: %lu",
        node->public_ip.c_str(), node->public_port, msg.hash64());
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

            assert(false);
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
                ZJC_DEBUG("sync key value %u_%u_%lu, handle sync value failed request hash: %lu, "
                    "net: %u, pool: %u, height: %lu",
                    network_id, 
                    sync_msg.sync_value_req().heights(i).pool_idx(),
                    sync_msg.sync_value_req().heights(i).height(),
                    network_id, 
                    sync_msg.sync_value_req().heights(i).pool_idx(),
                    sync_msg.sync_value_req().heights(i).height(),
                    msg_ptr->header.hash64());
                continue;
            }
            
#ifdef ENABLE_HOTSTUFF
            view_block::protobuf::ViewBlockItem pb_view_block;
            if (!prefix_db_->GetViewBlockInfo(
                        network_id,
                        sync_msg.sync_value_req().heights(i).pool_idx(),
                        sync_msg.sync_value_req().heights(i).height(),
                        &pb_view_block)) {
                ZJC_DEBUG("sync key value %u_%u_%lu, "
                    "handle sync value failed, view block info not found, request hash: %lu, "
                    "net: %u, pool: %u, height: %lu",
                    network_id, 
                    sync_msg.sync_value_req().heights(i).pool_idx(),
                    sync_msg.sync_value_req().heights(i).height(),
                    msg_ptr->header.hash64(),
                    network_id, 
                    sync_msg.sync_value_req().heights(i).pool_idx(),
                    sync_msg.sync_value_req().heights(i).height());
                continue;
            }

            pb_view_block.mutable_block_info()->CopyFrom(block);
#endif

            auto res = sync_res->add_res();
            res->set_network_id(network_id);
            res->set_pool_idx(sync_msg.sync_value_req().heights(i).pool_idx());
            res->set_height(sync_msg.sync_value_req().heights(i).height());
#ifdef ENABLE_HOTSTUFF
            res->set_value(pb_view_block.SerializeAsString());
#ifndef NDEBUG
            view_block::protobuf::QC proto_qc;
            assert(proto_qc.ParseFromString(pb_view_block.qc_str()));
            view_block::protobuf::QC proto_commit_qc;
            assert(proto_commit_qc.ParseFromString(pb_view_block.self_commit_qc_str()));
            block::protobuf::Block test_block;
            if (!prefix_db_->GetBlockWithHeight(
                    network::kRootCongressNetworkId,
                    network_id % common::kImmutablePoolSize,
                    proto_qc.elect_height(),
                    &test_block)) {
                ZJC_INFO("sync key value %u_%u_%lu, failed get block with height net: %u, pool: %u, height: %lu",
                    network_id, 
                    sync_msg.sync_value_req().heights(i).pool_idx(),
                    sync_msg.sync_value_req().heights(i).height(),
                    network::kRootCongressNetworkId, network_id, proto_qc.elect_height());
                // assert(false);
                return;
            }

            assert(test_block.tx_list_size() > 0);
            std::stringstream ss;
            ss << proto_commit_qc.view() << proto_commit_qc.view_block_hash()
                << proto_commit_qc.commit_view_block_hash()
                << proto_commit_qc.elect_height() << proto_commit_qc.leader_idx();
            std::string msg = ss.str();
            auto msg_hash = common::Hash::keccak256(msg); 
            ZJC_INFO("sync key value %u_%u_%lu, sync success get block with height net: %u, pool: %u, "
                "qc height: %lu, commit elect height: %lu, net: %u, "
                "block elect height: %lu, view: %lu, view_block_hash: %s, "
                "commit_view_block_hash: %s, elect_height: %lu, leader_idx: %u, msg_hash: %s",
                network_id,
                sync_msg.sync_value_req().heights(i).pool_idx(),
                sync_msg.sync_value_req().heights(i).height(),
                network::kRootCongressNetworkId,
                network_id,
                proto_qc.elect_height(),
                proto_commit_qc.elect_height(),
                test_block.network_id(),
                test_block.electblock_height(),
                proto_commit_qc.view(),
                common::Encode::HexEncode(proto_commit_qc.view_block_hash()).c_str(),
                common::Encode::HexEncode(proto_commit_qc.commit_view_block_hash()).c_str(),
                proto_commit_qc.elect_height(),
                proto_commit_qc.leader_idx(),
                common::Encode::HexEncode(msg_hash).c_str());
#endif
#else
            res->set_value(block.SerializeAsString());
#endif
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
        if (iter->has_height()) {
            key = std::to_string(iter->network_id()) + "_" +
                std::to_string(iter->pool_idx()) + "_" +
                std::to_string(iter->height());
        ZJC_DEBUG("now handle kv response hash64: %lu, key: %s", msg_ptr->header.hash64(), key.c_str());
#ifndef ENABLE_HOTSTUFF
            auto block_item = std::make_shared<block::protobuf::Block>();
            if (block_item->ParseFromString(iter->value())) {
                if (block_agg_valid_func_ != nullptr) {
                    int res = block_agg_valid_func_(*block_item);
                    if (res == -1) {
                        continue;
                    }

                    auto& pool_blocks = net_with_pool_blocks_[block_item->network_id()].pool_blocks;
                    if (res == 0) {
                        ZJC_DEBUG("0 success handle network new block: %u, %u, %lu", 
                            block_item->network_id(), block_item->pool_index(), block_item->height());
                        pool_blocks[block_item->pool_index()][block_item->height()] = nullptr;
                        auto thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[block_item->pool_index()];
                        bft_block_queues_[thread_idx].push(block_item);
                    } else {
                        pool_blocks[block_item->pool_index()][block_item->height()] = block_item;
                    }
                }
            }

#else
            auto pb_vblock = std::make_shared<view_block::protobuf::ViewBlockItem>();
            if (!pb_vblock->ParseFromString(iter->value())) {
                ZJC_ERROR("pb vblock parse failed");
                assert(false);
                continue;
            }
            if (!pb_vblock->has_self_commit_qc_str()) {
                ZJC_ERROR("pb vblock has no qc");
                assert(false);
                continue;
            }

            if (!view_block_synced_callback_) {
                ZJC_ERROR("no view block synced callback inited");
                assert(false);
                continue;
            }
            int res = view_block_synced_callback_(pb_vblock.get());
            if (res == -1) {
                continue;
            }

            if (res == 1) {
                ZJC_ERROR("no elect item, %u_%u_%lu",
                    network::kRootCongressNetworkId,
                    pb_vblock->block_info().network_id(),
                    pb_vblock->block_info().electblock_height());
                continue;
            }
                
            if (res == 0) {
                ZJC_DEBUG("0 success handle network new view block: %u, %u, %lu, key: %s", 
                    pb_vblock->block_info().network_id(),
                    pb_vblock->block_info().pool_index(),
                    pb_vblock->block_info().height(),
                    key.c_str());
                auto thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[pb_vblock->block_info().pool_index()];

                vblock_queues_[thread_idx].push(pb_vblock);
            }            

#endif
        }

        auto tmp_iter = synced_map_.find(key);
        if (tmp_iter != synced_map_.end()) {
            tmp_iter->second->responsed_timeout_us = now_tm_us + kSyncTimeoutPeriodUs;
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

#ifndef ENABLE_HOTSTUFF
void KeyValueSync::CheckNotCheckedBlocks() {
    if (block_agg_valid_func_ == nullptr) {
        return;
    }

    for (uint32_t sharding_id = network::kRootCongressNetworkId;
            sharding_id <= max_sharding_id_; ++sharding_id) {
        for (uint32_t pool_idx = 0; pool_idx < common::kInvalidPoolIndex; ++pool_idx) {
            auto& pool_blocks = net_with_pool_blocks_[sharding_id].pool_blocks[pool_idx];
            if (pool_blocks.empty()) {
                continue;
            }

            auto iter = pool_blocks.begin();
            while (iter != pool_blocks.end()) {
                if (iter->second == nullptr) {
                    iter = pool_blocks.erase(iter);
                    continue;
                }

                int res = block_agg_valid_func_(*iter->second);
                if (res == -1) {
                    break;
                }

                ZJC_DEBUG("0 success handle network new block: %u, %u, %lu", 
                    iter->second->network_id(), iter->second->pool_index(), iter->second->height());
                auto thread_idx = common::GlobalInfo::Instance()->pools_with_thread()[
                    iter->second->pool_index()];
                bft_block_queues_[thread_idx].push(iter->second);
                ZJC_DEBUG("check not signed blocks success: %u, %u, %lu",
                    sharding_id, pool_idx, iter->first);
                iter = pool_blocks.erase(iter);
            }
        }
    }
}
#endif

void KeyValueSync::CheckSyncTimeout() {
    auto now_tm_us = common::TimeUtils::TimestampUs();
    for (auto iter = synced_map_.begin(); iter != synced_map_.end();) {
        if (iter->second->sync_times >= kSyncMaxRetryTimes ||
                iter->second->responsed_timeout_us <= now_tm_us) {
            added_key_set_.erase(iter->second->key);
            iter = synced_map_.erase(iter);
            continue;
        }

        if (iter->second->sync_tm_us + 1000000 >= now_tm_us) {
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
