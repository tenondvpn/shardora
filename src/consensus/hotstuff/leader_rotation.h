#pragma once

#include <common/node_members.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

class LeaderRotation {
public:
    LeaderRotation(const std::shared_ptr<ViewBlockChain>&, const std::shared_ptr<ElectInfo>&);
    ~LeaderRotation();

    LeaderRotation(const LeaderRotation&) = delete;
    LeaderRotation& operator=(const LeaderRotation&) = delete;

    // Generally committed_view_block.view is used
    common::BftMemberPtr GetLeader();
    
    inline int32_t GetLocalMemberIdx() const {
        if (!elect_info_->GetElectItem()) {
            return -1;
        }
        return elect_info_->GetElectItem()->LocalMember()->index;
    }
private:
    inline common::MembersPtr Members() const {
        return elect_info_->GetElectItem()->Members(); 
    }
    
    std::shared_ptr<ViewBlockChain> chain_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
};

} // namespace consensus

} // namespace shardora

