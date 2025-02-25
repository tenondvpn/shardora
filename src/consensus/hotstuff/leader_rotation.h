#pragma once

#include <common/node_members.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include "vss/vss_manager.h"

namespace shardora {

namespace hotstuff {

static const uint32_t TIME_EPOCH_TO_CHANGE_LEADER_S = 30; // 单位 s, 时间边界时会造成 Leader 不一致而卡顿，不过活性可以保证

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
    common::BftMemberPtr GetLeader() const {
        auto members = Members(common::GlobalInfo::Instance()->network_id());
        if (members->empty()) {
            return nullptr;
        }

        auto index = 0; // pool_idx_ % members->size();
        return (*members)[index];
    }

    inline common::BftMemberPtr GetExpectedLeader() const {
        return GetLeader();
    }

    inline common::BftMemberPtr GetMember(uint32_t member_index) const {
        auto members = Members(common::GlobalInfo::Instance()->network_id());
        if (member_index >= members->size()) {
            return nullptr;
        }

        return (*members)[member_index];
    }
    
    inline uint32_t GetLocalMemberIdx() const {
        auto sharding_id = common::GlobalInfo::Instance()->network_id();
        assert(elect_info_ != nullptr);
        auto elect_item = elect_info_->GetElectItemWithShardingId(sharding_id);
        if (elect_item == nullptr) {
            // assert(false);
            return common::kInvalidUint32;
        }

        auto local_mem_ptr = elect_info_->GetElectItemWithShardingId(sharding_id)->LocalMember();
        if (local_mem_ptr == nullptr) {
            // assert(false);
            return common::kInvalidUint32;
        }

        return local_mem_ptr->index;
    }

    void SetExpectedLeader(const common::BftMemberPtr& leader) {
        expected_leader_ = leader;
    }

    void SetExtraNonce(const std::string& extra_nonce) {
        extra_nonce_ = extra_nonce; 
    }

    inline uint32_t MemberSize(uint32_t sharding_id) const {
        auto elect_item = elect_info_->GetElectItemWithShardingId(sharding_id);
        if (!elect_item) {
            return common::kInvalidUint32;
        }
        
        return elect_item->Members()->size(); 
    }

private:
    inline common::MembersPtr Members(uint32_t sharding_id) const {
        auto elect_item = elect_info_->GetElectItemWithShardingId(sharding_id);
        if (!elect_item) {
            return std::make_shared<common::Members>();
        }
        return elect_item->Members(); 
    }

    common::BftMemberPtr getLeaderByRate(uint64_t random_hash);
    common::BftMemberPtr getLeaderByRandom(uint64_t random_hash);

    uint32_t pool_idx_;
    std::shared_ptr<ViewBlockChain> chain_ = nullptr;
    std::shared_ptr<ElectInfo> elect_info_ = nullptr;
    std::string extra_nonce_ = "";
    // 由于 Leader 的选择会受时间戳影响，需要记录一个 expected_leader 解决跨时间戳边界时 leader 不一致的问题
    common::BftMemberPtr expected_leader_; 
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
};

} // namespace consensus

} // namespace shardora

