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
    leader_last_commit_views_.resize(members->size());
    for (uint32_t i = 0; i < members->size(); i++) {
        member_consen_stats_[(*members)[i]->index] = std::make_shared<MemberConsensusStat>();
        leader_last_commit_views_[(*members)[i]->index] = 0;
    }
    
}

ConsensusStat::~ConsensusStat() {}

Status ConsensusStat::Accept(std::shared_ptr<ViewBlock>& v_block, uint32_t add_succ_num) {
    auto committed_consen_stat = GetMemberConsensusStat(
        v_block->qc().leader_idx());
    return Status::kSuccess;
}

Status ConsensusStat::Commit(const std::shared_ptr<ViewBlock> &v_block) {
    if (!v_block) {
        return Status::kError;
    }

    // Filter out old Commits
    auto last_view = leader_last_commit_views_[v_block->qc().leader_idx()];
    if (last_view >= v_block->qc().view()) {
        return Status::kSuccess;
    }
    leader_last_commit_views_[v_block->qc().leader_idx()] =
        v_block->qc().view();


    // SetMemberConsensusStat(
    //     v_block->qc().leader_idx(), 
    //     v_block->leader_consen_stat());

    // std::string ret;
    // auto all_consen_stats = GetAllConsensusStats();
    // for (uint32_t idx = 0; idx < all_consen_stats.size(); idx++) {
    //     ret += std::to_string(idx) + ": " + std::to_string(all_consen_stats[idx]->succ_num) + ", ";
    // }
    // SHARDORA_DEBUG("pool: %d get all stat: %s", ret.c_str());
    
    
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora
