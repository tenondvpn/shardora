#include "elect/elect_waiting_nodes.h"

#include "common/hash.h"
#include "network/network_utils.h"
#include "network/dht_manager.h"
#include "network/route.h"
#include "dht/base_dht.h"
#include "elect/elect_pool_manager.h"
#include "elect/elect_manager.h"
#include "elect/nodes_stoke_manager.h"
#include "elect/elect_proto.h"
#include "protos/elect.pb.h"
#include "protos/transport.pb.h"

namespace zjchain {

namespace elect {

ElectWaitingNodes::ElectWaitingNodes(
        ElectManager* elect_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<NodesStokeManager>& stoke_mgr,
        uint32_t waiting_shard_id, ElectPoolManager* pool_manager)
        : elect_mgr_(elect_mgr),
          security_ptr_(security_ptr),
          stoke_mgr_(stoke_mgr),
          waiting_shard_id_(waiting_shard_id),
          pool_manager_(pool_manager) {
    waiting_nodes_tick_.CutOff(
        kWaitingHeartbeatPeriod,
        std::bind(&ElectWaitingNodes::WaitingNodesUpdate, this));
}

ElectWaitingNodes::~ElectWaitingNodes() {}

void ElectWaitingNodes::UpdateWaitingNodes(
        const std::string& root_node_id,
        const std::string& balance_hash_256,
        const common::BloomFilter& nodes_filter) {
    std::lock_guard<std::mutex> guard(all_nodes_waiting_map_mutex_);
    std::string coming_id = root_node_id;
    //  + std::to_string(
    //     tmblock::TimeBlockManager::Instance()->LatestTimestamp());
    if (coming_root_nodes_.find(coming_id) != coming_root_nodes_.end()) {
        return;
    }

    coming_root_nodes_.insert(coming_id);
    auto member_index = elect_mgr_->GetMemberIndex(
        network::kRootCongressNetworkId,
        root_node_id);
    if (member_index == elect::kInvalidMemberIndex) {
        return;
    }

    auto local_all_waiting_bloom_filter = common::BloomFilter(
        kBloomfilterWaitingSize,
        kBloomfilterWaitingHashCount);
    std::vector<NodeDetailPtr> local_all_waiting_nodes;
    std::string hash_256;
    GetAllValidHeartbeatNodes(
        true,
        0,
        &hash_256,
        local_all_waiting_bloom_filter,
        local_all_waiting_nodes);
    if (hash_256 != balance_hash_256) {
        return;
    }

    std::sort(
        local_all_waiting_nodes.begin(),
        local_all_waiting_nodes.end(),
        ElectNodeIdCompare);
    WaitingListPtr wait_ptr = std::make_shared<WaitingList>();
    std::string all_nodes_ids;
    for (auto iter = local_all_waiting_nodes.begin();
            iter != local_all_waiting_nodes.end(); ++iter) {
        if (!nodes_filter.Contain(common::Hash::Hash64((*iter)->id))) {
//             continue;
            return;
        }
        
        wait_ptr->nodes_vec.push_back(*iter);
        all_nodes_ids += (*iter)->id;
    }

    wait_ptr->nodes_hash = common::Hash::Hash64(all_nodes_ids);
    auto iter = all_nodes_waiting_map_.find(wait_ptr->nodes_hash);
    if (iter == all_nodes_waiting_map_.end()) {
        wait_ptr->added_nodes.insert(root_node_id);
        all_nodes_waiting_map_[wait_ptr->nodes_hash] = wait_ptr;
    } else {
        iter->second->added_nodes.insert(root_node_id);
        if (iter->second->added_nodes.size() > max_nodes_count_) {
            max_nodes_count_ = iter->second->added_nodes.size();
            max_nodes_hash_ = wait_ptr->nodes_hash;
        }
    }
}

void ElectWaitingNodes::OnTimeBlock(uint64_t tm_block_tm) {
    std::lock_guard<std::mutex> guard(all_nodes_waiting_map_mutex_);
    if (got_valid_nodes_tm_ >= tm_block_tm) {
        return;
    }

    got_valid_nodes_tm_ = tm_block_tm;
}

void ElectWaitingNodes::GetAllValidNodes(
        common::BloomFilter& nodes_filter,
        std::vector<NodeDetailPtr>& nodes) {
//     std::vector<WaitingListPtr> waiting_nodes;
    WaitingListPtr wait_ptr = nullptr;
    {
        std::lock_guard<std::mutex> guard(all_nodes_waiting_map_mutex_);
        if (max_nodes_hash_ == 0) {
            return;
        }

        auto iter = all_nodes_waiting_map_.find(max_nodes_hash_);
        if (iter == all_nodes_waiting_map_.end()) {
            return;
        }

        wait_ptr = iter->second;
//         for (auto iter = all_nodes_waiting_map_.begin();
//                 iter != all_nodes_waiting_map_.end(); ++iter) {
//             waiting_nodes.push_back(iter->second);
//         }

        all_nodes_waiting_map_.clear();
        coming_root_nodes_.clear();
        max_nodes_count_ = 0;
        max_nodes_hash_ = 0;
    }

//     if (waiting_nodes.empty()) {
//         return;
//     }
// 
//     std::sort(waiting_nodes.begin(), waiting_nodes.end(), WaitingNodeCountCompare);
//     auto iter = waiting_nodes.begin();
    for (auto siter = wait_ptr->nodes_vec.begin(); siter != wait_ptr->nodes_vec.end(); ++siter) {
        ELECT_ERROR("all_nodes_waiting_map_ size: %d, waiting_shard_id_: %d, id: %s",
            wait_ptr->nodes_vec.size(), waiting_shard_id_, common::Encode::HexEncode((*siter)->id).c_str());
        if (elect_mgr_->IsIdExistsInAnyShard(
                waiting_shard_id_ - network::kConsensusWaitingShardOffset,
                (*siter)->id)) {
            continue;
        }

        nodes.push_back(*siter);
        nodes_filter.Add(common::Hash::Hash64((*siter)->id));
    }

    std::sort(nodes.begin(), nodes.end(), ElectNodeIdCompare);
}

void ElectWaitingNodes::AddNewNode(NodeDetailPtr& node_ptr) {
//     ELECT_DEBUG("waiting_shard_id_: %d, add new node: %s, %s:%d",
//         waiting_shard_id_,
//         common::Encode::HexEncode(node_ptr->id).c_str(),
//         node_ptr->public_ip.c_str(),
//         node_ptr->public_port);
    std::lock_guard<std::mutex> guard(node_map_mutex_);
    auto iter = node_map_.find(node_ptr->id);
    if (iter != node_map_.end()) {
        iter->second->public_ip = node_ptr->public_ip;
        iter->second->public_port = node_ptr->public_port;
        iter->second->dht_key = node_ptr->dht_key;
    } else {
        node_map_[node_ptr->id] = node_ptr;
    }
}

void ElectWaitingNodes::RemoveNodes(const std::vector<NodeDetailPtr>& nodes) {
    std::lock_guard<std::mutex> guard(node_map_mutex_);
    for (auto iter = nodes.begin(); iter != nodes.end(); ++iter) {
        auto niter = node_map_.find((*iter)->id);
        if (niter != node_map_.end()) {
            node_map_.erase(niter);
        }
    }
}

void ElectWaitingNodes::GetAllValidHeartbeatNodes(
        bool no_delay,
        uint64_t time_offset_milli,
        std::string* hash_256,
        common::BloomFilter& nodes_filter,
        std::vector<NodeDetailPtr>& nodes) {
    std::unordered_map<std::string, NodeDetailPtr> node_map;
    {
        node_map = node_map_;
    }

    auto now_tm = std::chrono::steady_clock::now();
    auto now_hb_tm = (std::chrono::steady_clock::now().time_since_epoch().count()
        - (1000000000llu * 1800llu)) / (1000000000llu * 300llu);
    std::vector<NodeDetailPtr> choosed_nodes;
    std::unordered_set<std::string> added_node_ids;
    std::unordered_set<std::string> added_node_ip;  // same ip just one node
    for (auto iter = node_map.begin(); iter != node_map.end(); ++iter) {
        if (!no_delay) {
            if (elect_mgr_->IsIdExistsInAnyShard(
                    waiting_shard_id_ - network::kConsensusWaitingShardOffset,
                    iter->first)) {
                continue;
            }

            // for fts poise
            if (time_offset_milli * 1000 > kElectAvailableJoinTime) {
                time_offset_milli = kElectAvailableJoinTime / 1000;
            }

            auto valid_join_time = iter->second->join_tm +
                std::chrono::microseconds(kElectAvailableJoinTime - time_offset_milli * 1000);
            if (valid_join_time > now_tm) {
                continue;
            }

            uint32_t succ_hb_count = 0;
            uint32_t fail_hb_count = 0;
            std::lock_guard<std::mutex> guard(iter->second->heartbeat_mutex);
            for (auto hb_iter = iter->second->heatbeat_succ_count.begin();
                    hb_iter != iter->second->heatbeat_succ_count.end();) {
                if (hb_iter->first < now_hb_tm) {
                    iter->second->heatbeat_succ_count.erase(hb_iter++);
                } else {
                    succ_hb_count += hb_iter->second;
                }
            }

            for (auto hb_iter = iter->second->heatbeat_fail_count.begin();
                hb_iter != iter->second->heatbeat_fail_count.end();) {
                if (hb_iter->first < now_hb_tm) {
                    iter->second->heatbeat_fail_count.erase(hb_iter++);
                } else {
                    fail_hb_count += hb_iter->second;
                }
            }

            if (succ_hb_count < 2 * fail_hb_count) {
                continue;
            }
        }

        nodes_filter.Add(common::Hash::Hash64(iter->second->id));
        nodes.push_back(iter->second);
    }

    if (nodes.empty()) {
        return;
    }

    std::sort(nodes.begin(), nodes.end(), ElectNodeIdCompare);
    std::string blance_str;
    for (auto iter = nodes.begin(); iter != nodes.end(); ++iter) {
        auto balance = stoke_mgr_->GetAddressStoke((*iter)->id);
        blance_str += std::to_string(balance) + "_";
    }

    *hash_256 = common::Hash::Hash256(blance_str);
}

void ElectWaitingNodes::SendConsensusNodes(uint64_t time_block_tm) {
    auto dht = network::DhtManager::Instance()->GetDht(
        common::GlobalInfo::Instance()->network_id());
    if (!dht) {
        return;
    }

    last_send_tm_ = time_block_tm;
    auto local_all_waiting_bloom_filter = common::BloomFilter(
        kBloomfilterWaitingSize,
        kBloomfilterWaitingHashCount);
    std::vector<NodeDetailPtr> local_all_waiting_nodes;
    std::string hash_256;
    GetAllValidHeartbeatNodes(
        false,
        0,
        &hash_256,
        local_all_waiting_bloom_filter,
        local_all_waiting_nodes);
    if (!local_all_waiting_nodes.empty()) {
        transport::protobuf::Header msg;
        bool res = elect::ElectProto::CreateElectWaitingNodes(
            security_ptr_,
            dht->local_node(),
            waiting_shard_id_,
            hash_256,
            local_all_waiting_bloom_filter,
            msg);
        if (res) {
            network::Route::Instance()->Send(nullptr);
        }
    }
}

void ElectWaitingNodes::WaitingNodesUpdate() {
    SendConsensusNodes(got_valid_nodes_tm_);
    waiting_nodes_tick_.CutOff(
        kWaitingHeartbeatPeriod,
        std::bind(&ElectWaitingNodes::WaitingNodesUpdate, this));
}

void ElectWaitingNodes::UpdateWaitingNodeStoke() {
    WaitingListPtr wait_ptr = nullptr;
    {
        std::lock_guard<std::mutex> guard(all_nodes_waiting_map_mutex_);
        if (max_nodes_hash_ == 0) {
            return;
        }

        auto iter = all_nodes_waiting_map_.find(max_nodes_hash_);
        if (iter == all_nodes_waiting_map_.end()) {
            return;
        }

        wait_ptr = iter->second;
    }

    std::vector<std::string> ids;
    for (auto siter = wait_ptr->nodes_vec.begin(); siter != wait_ptr->nodes_vec.end(); ++siter) {
        ids.push_back((*siter)->id);
    }

    stoke_mgr_->SyncAddressStoke(ids);
}

};  // namespace elect

};  // namespace zjchain
