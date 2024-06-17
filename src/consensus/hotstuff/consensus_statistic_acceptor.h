#pragma once

#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

// 用于计算和提交 leader 的共识数据
class ConsensusStatAcceptor {
public:
    ConsensusStatAcceptor(
            uint32_t pool_idx,
            std::shared_ptr<ElectInfo>& elect_info,
            std::shared_ptr<ViewBlockChain>& view_block_chain);
    ~ConsensusStatAcceptor();

    ConsensusStatAcceptor(const ConsensusStatAcceptor&) = delete;
    ConsensusStatAcceptor& operator=(const ConsensusStatAcceptor&) = delete;

    void OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        const common::MembersPtr& members);
    // 接收并计算 leader 的共识统计数据
    Status Accept(std::shared_ptr<ViewBlock>& v_block);
    // 提交并生效共识统计数据
    Status Commit(const std::shared_ptr<ViewBlock>& v_block);

    void SetMemberConsensusStat(
            uint32_t member_idx,
            const std::shared_ptr<MemberConsensusStat>& member_consen_stat) {
        if (member_consen_stats_.size() > member_idx) {
            member_consen_stats_[member_idx] = member_consen_stat;
        }
    }

    inline std::vector<std::shared_ptr<MemberConsensusStat>> GetAllConsensusStats() {
        return member_consen_stats_;
    }

    std::shared_ptr<MemberConsensusStat> GetMemberConsensusStat(uint32_t member_idx) {
        if (member_consen_stats_.size() <= member_idx) {
            return nullptr;
        }
        return member_consen_stats_[member_idx];
    }    

private:
    uint32_t pool_idx_;
    std::unordered_map<uint32_t, View> leader_last_commit_view_map_; // member_index => View, 记录所有 leader 最后一次提交的 View
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<ViewBlockChain> view_block_chain_;
    std::vector<std::shared_ptr<MemberConsensusStat>> member_consen_stats_;
};

} // namespace hotstuff

} // namespace shardora

