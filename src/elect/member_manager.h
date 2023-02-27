#pragma once

#include <unordered_map>
#include <mutex>

#include "dht/dht_utils.h"
#include "elect/elect_node_detail.h"
#include "elect/elect_utils.h"

namespace zjchain {

namespace elect {

class MemberManager {
public:
    MemberManager();
    ~MemberManager();
    void SetNetworkMember(
        uint32_t network_id,
        common::MembersPtr& members_ptr,
        elect::NodeIndexMapPtr& node_index_map,
        int32_t leader_count);
    uint32_t GetMemberIndex(uint32_t network_id, const std::string& node_id);
    common::MembersPtr GetNetworkMembers(uint32_t network_id);
    common::BftMemberPtr GetMember(uint32_t network_id, const std::string& node_id);
    common::BftMemberPtr GetMember(uint32_t network_id, uint32_t index);
    uint32_t GetMemberCount(uint32_t network_id);
    int32_t GetNetworkLeaderCount(uint32_t network_id);

private:
    common::MembersPtr* network_members_;
    elect::NodeIndexMapPtr* node_index_map_;
    std::unordered_map<uint32_t, int32_t> leader_count_map_;
    std::mutex all_mutex_;

    DISALLOW_COPY_AND_ASSIGN(MemberManager);
};

}  // namespace elect

}  // namespace zjchain
