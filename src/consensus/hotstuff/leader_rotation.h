#pragma once

#include <common/node_members.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace consensus {

class LeaderRotation {
public:
    explicit LeaderRotation(const std::shared_ptr<ViewBlockChain>&);
    ~LeaderRotation();

    LeaderRotation(const LeaderRotation&) = delete;
    LeaderRotation& operator=(const LeaderRotation&) = delete;

    // Generally committed_view_block.view is used
    uint32_t GetLeaderIdx();
    void OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height, const common::MembersPtr& members);
private:
    common::MembersPtr members_;
    uint64_t latest_elect_height_;
    std::shared_ptr<ViewBlockChain> chain_;
};

} // namespace consensus

} // namespace shardora

