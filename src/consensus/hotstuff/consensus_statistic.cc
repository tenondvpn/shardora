#include <common/global_info.h>
#include <common/utils.h>
#include <consensus/hotstuff/consensus_statistic.h>
#include <consensus/hotstuff/types.h>

namespace shardora {

namespace hotstuff {

ConsensusStat::ConsensusStat(
        uint32_t pool_idx,
        const common::MembersPtr& members) : pool_idx_(pool_idx) {
    member_consen_stats_.resize(members->size());
    for (uint32_t i = 0; i < members->size(); i++) {
        member_consen_stats_[(*members)[i]->index] = std::make_shared<MemberConsensusStat>();
    }
}

ConsensusStat::~ConsensusStat() {}

Status ConsensusStat::Accept(std::shared_ptr<ViewBlock>& v_block) {
    auto committed_consen_stat = GetMemberConsensusStat(v_block->leader_idx);
    v_block->leader_consen_stat = std::make_shared<MemberConsensusStat>(
            committed_consen_stat->succ_num+1, committed_consen_stat->fail_num);
    
    return Status::kSuccess;
}

Status ConsensusStat::Commit(const std::shared_ptr<ViewBlock> &v_block) {
    ZJC_DEBUG("pool: %d consen stat commit leader: %d, view: %lu, succ: %lu", pool_idx_, v_block->leader_idx, v_block->view, v_block->leader_consen_stat->succ_num);
    if (!v_block || !v_block->leader_consen_stat) {
        return Status::kError;
    }

    // 旧的 Commit 过滤掉
    auto it = leader_last_commit_view_map_.find(v_block->leader_idx);
    if (it != leader_last_commit_view_map_.end()) {
        if (it->second >= v_block->view) {
            return Status::kSuccess;
        }
    }
    leader_last_commit_view_map_[v_block->leader_idx] = v_block->view;


    ZJC_DEBUG("pool: %d set consen stat leader: %d, view: %lu, succ: %lu", pool_idx_, v_block->leader_idx, v_block->view, v_block->leader_consen_stat->succ_num);
    SetMemberConsensusStat(v_block->leader_idx, v_block->leader_consen_stat);

    // std::string ret;
    // auto all_consen_stats = GetAllConsensusStats();
    // for (uint32_t idx = 0; idx < all_consen_stats.size(); idx++) {
    //     ret += std::to_string(idx) + ": " + std::to_string(all_consen_stats[idx]->succ_num) + ", ";
    // }
    // ZJC_DEBUG("pool: %d get all stat: %s", ret.c_str());
    
    
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

