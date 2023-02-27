#include "elect/elect_pool.h"

#include "common/fts_tree.h"
#include "common/global_info.h"
#include "network/network_utils.h"
#include "elect/nodes_stoke_manager.h"

namespace zjchain {

namespace elect {

ElectPool::ElectPool(uint32_t netid, std::shared_ptr<NodesStokeManager>& stoke_mgr)
    : network_id_(netid), stoke_mgr_(stoke_mgr) {}

ElectPool::~ElectPool() {}

void ElectPool::ReplaceWithElectNodes(std::vector<NodeDetailPtr>& nodes) {
    std::lock_guard<std::mutex> guard(node_map_mutex_);
    elect_nodes_.swap(nodes);
}

void ElectPool::GetAllValidNodes(
        common::BloomFilter& nodes_filter,
        std::vector<NodeDetailPtr>& nodes) {
    std::unordered_map<std::string, NodeDetailPtr> node_map;
    {
        if ((network_id_ >= network::kConsensusShardBeginNetworkId &&
                network_id_ < network::kConsensusShardEndNetworkId) ||
                network_id_ == network::kRootCongressNetworkId) {
            std::lock_guard<std::mutex> guard(node_map_mutex_);
            nodes = elect_nodes_;
        }

//         for (auto iter = nodes.begin(); iter != nodes.end(); ++iter) {
//             nodes_filter.Add(common::Hash::Hash64((*iter)->id));
//         }
    }

//     std::sort(nodes.begin(), nodes.end(), ElectNodeIdCompare);
}

void ElectPool::UpdateNodesStoke() {
    std::vector<NodeDetailPtr> nodes;
    {
        std::lock_guard<std::mutex> guard(node_map_mutex_);
        nodes = elect_nodes_;
    }

    std::vector<std::string> ids;
    for (auto siter = nodes.begin(); siter != nodes.end(); ++siter) {
        ids.push_back((*siter)->id);
    }

    stoke_mgr_->SyncAddressStoke(ids);
}

};  // namespace elect

};  //  namespace zjchain
