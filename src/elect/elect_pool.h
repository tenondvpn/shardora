#pragma once

#include <unordered_map>
#include <mutex>

#include "common/bloom_filter.h"
#include "elect/elect_utils.h"
#include "elect/elect_node_detail.h"
#include "elect/nodes_stoke_manager.h"

namespace zjchain {

namespace elect {

class ElectPool {
public:
    explicit ElectPool(uint32_t net_id, std::shared_ptr<NodesStokeManager>& stoke_mgr);
    ~ElectPool();
    void ReplaceWithElectNodes(std::vector<NodeDetailPtr>& nodes);
    // now shard min balance and max balance is 2/3 nodes middle balance
    void GetAllValidNodes(
        common::BloomFilter& nodes_filter,
        std::vector<NodeDetailPtr>& nodes);
    void UpdateNodesStoke();

private:
    void CreateFtsTree(const std::vector<NodeDetailPtr>& src_nodes);

    std::mutex node_map_mutex_;
    std::vector<NodeDetailPtr> elect_nodes_;
    uint32_t network_id_{ 0 };
    uint64_t smooth_min_balance_{ 0 };
    uint64_t smooth_max_balance_{ 0 };
    std::shared_ptr<NodesStokeManager> stoke_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ElectPool);
};

typedef std::shared_ptr<ElectPool> ElectPoolPtr;

};  // namespace elect

};  //  namespace zjchain
