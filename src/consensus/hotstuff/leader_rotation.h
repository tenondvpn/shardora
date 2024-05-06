#pragma once

#include <common/node_members.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

class LeaderRotation {
public:
    LeaderRotation(
            const uint32_t& pool_idx,
            const std::shared_ptr<ViewBlockChain>&,
            const std::shared_ptr<ElectInfo>&);
    ~LeaderRotation();

    LeaderRotation(const LeaderRotation&) = delete;
    LeaderRotation& operator=(const LeaderRotation&) = delete;

    // Generally committed_view_block.view is used
    common::BftMemberPtr GetLeader();
    
    inline uint32_t GetLocalMemberIdx() const {
        assert(elect_info_ != nullptr);
        assert(elect_info_->GetElectItem());
        return elect_info_->GetElectItem()->LocalMember()->index;
    }
private:
    inline common::MembersPtr Members() const {
        auto elect_item = elect_info_->GetElectItem();
        if (!elect_item) {
            return std::make_shared<common::Members>();
        }
        return elect_item->Members(); 
    }

    uint32_t pool_idx_;
    std::shared_ptr<ViewBlockChain> chain_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
};

} // namespace consensus

} // namespace shardora

