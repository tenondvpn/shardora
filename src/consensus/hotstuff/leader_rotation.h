#pragma once

#include <common/node_members.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

class LeaderRotation {
public:
    LeaderRotation(const std::shared_ptr<ViewBlockChain>&, const std::shared_ptr<CryptoInfo>&);
    ~LeaderRotation();

    LeaderRotation(const LeaderRotation&) = delete;
    LeaderRotation& operator=(const LeaderRotation&) = delete;

    // Generally committed_view_block.view is used
    common::BftMemberPtr GetLeader();
    
    inline uint32_t GetLocalMemberIdx() const {
        return crypto_info_->LocalMember()->index;
    }
private:
    inline common::MembersPtr Members() const {
        return crypto_info_->Members(); 
    }
    
    std::shared_ptr<ViewBlockChain> chain_ = nullptr;
    std::shared_ptr<CryptoInfo> crypto_info_ = nullptr;
};

} // namespace consensus

} // namespace shardora

