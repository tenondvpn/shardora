#pragma once

#include <unordered_map>
#include <mutex>

#include "common/bloom_filter.h"
#include "common/tick.h"
#include "elect/elect_utils.h"
#include "elect/elect_node_detail.h"
#include "elect/nodes_stoke_manager.h"
#include "security/security.h"

namespace zjchain {

namespace elect {

class ElectPoolManager;
class ElectManager;
class ElectWaitingNodes {
public:
    ElectWaitingNodes(
        ElectManager* elect_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<NodesStokeManager>& stoke_mgr,
        uint32_t waiting_shard_id,
         ElectPoolManager* pool_manager);
    ~ElectWaitingNodes();
    void UpdateWaitingNodes(
        const std::string& root_node_id,
        const std::string& balance_hash_256,
        const common::BloomFilter& nodes_filter);
    void AddNewNode(NodeDetailPtr& node_ptr);
    void RemoveNodes(const std::vector<NodeDetailPtr>& nodes);
    void GetAllValidNodes(
        common::BloomFilter& nodes_filter,
        std::vector<NodeDetailPtr>& nodes);
    void GetAllValidHeartbeatNodes(
        bool no_delay,
        uint64_t time_offset_milli,
        std::string* hash_256,
        common::BloomFilter& nodes_filter,
        std::vector<NodeDetailPtr>& nodes);
    void OnTimeBlock(uint64_t tm_block_tm);
    void UpdateWaitingNodeStoke();

private:
    void SendConsensusNodes(uint64_t time_block_tm);
    void WaitingNodesUpdate();

    static const uint64_t kWaitingHeartbeatPeriod = 30000000llu;

    ElectManager* elect_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    std::shared_ptr<NodesStokeManager> stoke_mgr_ = nullptr;
    uint32_t waiting_shard_id_{ 0 };
    ElectPoolManager* pool_manager_{ nullptr };
    std::unordered_map<std::string, NodeDetailPtr> node_map_;
    std::mutex node_map_mutex_;
    common::Tick waiting_nodes_tick_;
    uint64_t last_send_tm_{ 0 };
    std::unordered_map<uint64_t, WaitingListPtr> all_nodes_waiting_map_;
    std::mutex all_nodes_waiting_map_mutex_;
    std::unordered_set<std::string> coming_root_nodes_;
    uint64_t got_valid_nodes_tm_{ 0 };
    uint32_t max_nodes_count_{ 0 };
    uint64_t max_nodes_hash_{ 0 };

    DISALLOW_COPY_AND_ASSIGN(ElectWaitingNodes);
};

typedef std::shared_ptr<ElectWaitingNodes> ElectWaitingNodesPtr;

};  // namespace elect

};  // namespace zjchain
