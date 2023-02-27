#include "elect/member_manager.h"

#include <cassert>

#include "network/network_utils.h"

namespace zjchain {

namespace elect {

MemberManager::MemberManager() {
    network_members_ = new common::MembersPtr[network::kConsensusShardEndNetworkId];
    std::fill(
            network_members_,
            network_members_ + network::kConsensusShardEndNetworkId,
            nullptr);
    node_index_map_ = new elect::NodeIndexMapPtr[network::kConsensusShardEndNetworkId];
    std::fill(
            node_index_map_,
            node_index_map_ + network::kConsensusShardEndNetworkId,
            nullptr);
}

MemberManager::~MemberManager() {
    if (network_members_ != nullptr) {
        delete []network_members_;
    }

    if (node_index_map_ != nullptr) {
        delete[]node_index_map_;
    }
}

int32_t MemberManager::GetNetworkLeaderCount(uint32_t network_id) {
    std::lock_guard<std::mutex> guard(all_mutex_);
    auto iter = leader_count_map_.find(network_id);
    if (iter != leader_count_map_.end()) {
        return iter->second;
    }

    return 0;
}

void MemberManager::SetNetworkMember(
        uint32_t network_id,
        common::MembersPtr& members_ptr,
        elect::NodeIndexMapPtr& node_index_map,
        int32_t leader_count) {
    std::lock_guard<std::mutex> guard(all_mutex_);
    assert(network_id < network::kConsensusShardEndNetworkId);  // just shard
    assert(!members_ptr->empty());
    network_members_[network_id] = members_ptr;
    node_index_map_[network_id] = node_index_map;
    leader_count_map_[network_id] = leader_count;
}

common::MembersPtr MemberManager::GetNetworkMembers(uint32_t network_id) {
    std::lock_guard<std::mutex> guard(all_mutex_);
    assert(network_id < network::kConsensusShardEndNetworkId);  // just shard
    return network_members_[network_id];
}

uint32_t MemberManager::GetMemberCount(uint32_t network_id) {
    std::lock_guard<std::mutex> guard(all_mutex_);
    assert(network_id < network::kConsensusShardEndNetworkId);  // just shard
    if (network_members_[network_id] == nullptr) {
        return 0;
    }

    return network_members_[network_id]->size();
}

uint32_t MemberManager::GetMemberIndex(uint32_t network_id, const std::string& node_id) {
    std::lock_guard<std::mutex> guard(all_mutex_);
    assert(network_id < network::kConsensusShardEndNetworkId);  // just shard
    elect::NodeIndexMapPtr node_index_map = node_index_map_[network_id];
    if (node_index_map == nullptr) {
        return kInvalidMemberIndex;
    }

    assert(node_index_map != nullptr);
    assert(!node_index_map->empty());
    auto iter = node_index_map->find(node_id);
    if (iter == node_index_map->end()) {
        return elect::kInvalidMemberIndex;
    }

    return iter->second;
}

common::BftMemberPtr MemberManager::GetMember(
        uint32_t network_id,
        const std::string& node_id) {
    assert(network_id < network::kConsensusShardEndNetworkId);  // just shard
    uint32_t mem_index = GetMemberIndex(network_id, node_id);
    if (mem_index == elect::kInvalidMemberIndex) {
        return nullptr;
    }
    std::lock_guard<std::mutex> guard(all_mutex_);
    common::MembersPtr member_ptr = network_members_[network_id];
    assert(member_ptr != nullptr);
    assert(!member_ptr->empty());
    return (*member_ptr)[mem_index];
}

common::BftMemberPtr MemberManager::GetMember(uint32_t network_id, uint32_t index) {
    std::lock_guard<std::mutex> guard(all_mutex_);
    common::MembersPtr member_ptr = network_members_[network_id];
    if (member_ptr == nullptr) {
        return nullptr;
    }

    if (member_ptr->size() <= index) {
        return nullptr;
    }

    assert(member_ptr != nullptr);
    assert(!member_ptr->empty());
    return (*member_ptr)[index];
}

}  // namespace elect

}  // namespace zjchain
