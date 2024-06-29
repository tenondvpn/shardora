#pragma once

#include <consensus/hotstuff/types.h>
#include <common/node_members.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

// 用于计算和提交 leader 的共识数据
class ConsensusStat {
public:
    ConsensusStat(uint32_t pool_idx,
        const common::MembersPtr& members);
    ~ConsensusStat();

    ConsensusStat(const ConsensusStat&) = delete;
    ConsensusStat& operator=(const ConsensusStat&) = delete;
    
    // 提交并生效共识统计数据
    Status Accept(std::shared_ptr<ViewBlock>& v_block, uint32_t add_succ_num);
    Status Commit(const std::shared_ptr<ViewBlock>& v_block);

    inline const std::vector<std::shared_ptr<MemberConsensusStat>> GetAllConsensusStats() {
        return member_consen_stats_;
    }

    uint32_t TotalSuccNum() {
        uint32_t total = 0;
        for (const auto& stat : member_consen_stats_) {
            total += stat->succ_num;
        }
        return total;
    }

    const std::shared_ptr<MemberConsensusStat> GetMemberConsensusStat(uint32_t member_idx) {
        if (member_consen_stats_.size() <= member_idx) {
            return nullptr;
        }
        return member_consen_stats_[member_idx];
    }    

private:
    uint32_t pool_idx_;
    std::vector<View> leader_last_commit_views_; // member_index => View, 记录所有 leader 最后一次提交的 View
    std::vector<std::shared_ptr<MemberConsensusStat>> member_consen_stats_;

    void SetMemberConsensusStat(
            uint32_t member_idx,
            const std::shared_ptr<MemberConsensusStat>& member_consen_stat) {
        if (member_consen_stats_.size() > member_idx) {
            member_consen_stats_[member_idx] = member_consen_stat;
        }
    }    
};

} // namespace hotstuff

} // namespace shardora
