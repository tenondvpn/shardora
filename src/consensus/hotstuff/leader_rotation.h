#pragma once

#include <common/node_members.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

static const uint32_t TIME_EPOCH_TO_CHANGE_LEADER_S = 6000000; // 单位 s, 时间边界时会造成 Leader 不一致而卡顿，不过活性可以保证

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
    inline common::BftMemberPtr GetExpectedLeader() const {
        return expected_leader_;
    }
    
    inline uint32_t GetLocalMemberIdx() const {
        auto sharding_id = common::GlobalInfo::Instance()->network_id();
        assert(elect_info_ != nullptr);
        assert(elect_info_->GetElectItemWithShardingId(sharding_id));
        return elect_info_->GetElectItemWithShardingId(sharding_id)->LocalMember()->index;
    }

    void SetExpectedLeader(const common::BftMemberPtr& leader) {
        expected_leader_ = leader;
    }

    void SetExtraNonce(const std::string& extra_nonce) {
        extra_nonce_ = extra_nonce; 
    }
private:
    inline common::MembersPtr Members(uint32_t sharding_id) const {
        auto elect_item = elect_info_->GetElectItemWithShardingId(sharding_id);
        if (!elect_item) {
            return std::make_shared<common::Members>();
        }
        return elect_item->Members(); 
    }

    uint32_t pool_idx_;
    std::shared_ptr<ViewBlockChain> chain_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::string extra_nonce_ = "";
    // 由于 Leader 的选择会受时间戳影响，需要记录一个 expected_leader 解决跨时间戳边界时 leader 不一致的问题
    common::BftMemberPtr expected_leader_; 
};

} // namespace consensus

} // namespace shardora

