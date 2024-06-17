#include <common/global_info.h>
#include <consensus/hotstuff/consensus_statistic_acceptor.h>
#include <consensus/hotstuff/types.h>

namespace shardora {

namespace hotstuff {

ConsensusStatAcceptor::ConsensusStatAcceptor(
        uint32_t pool_idx,
        std::shared_ptr<ElectInfo>& elect_info,
        std::shared_ptr<ViewBlockChain>& view_block_chain) :
    pool_idx_(pool_idx), elect_info_(elect_info), view_block_chain_(view_block_chain) {}

ConsensusStatAcceptor::~ConsensusStatAcceptor() {}

void ConsensusStatAcceptor::OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        const common::MembersPtr& members) {
    for (uint32_t i = 0; i < members->size(); i++) {
        auto& mem = (*members)[i];
        member_consen_stats_[mem->index] = std::make_shared<MemberConsensusStat>();
    }    
}

Status ConsensusStatAcceptor::Accept(std::shared_ptr<ViewBlock> &v_block) {
    // 具体当前分支上未提交的 leader 的分数，计算出 v_block 中 leader 的分数
    return Status::kSuccess;
}

Status ConsensusStatAcceptor::Commit(const std::shared_ptr<ViewBlock> &v_block) {
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


    ZJC_DEBUG("pool: %d set consen stat leader: %d, view: %lu, succ: %lu", pool_idx_, v_block->view, v_block->leader_idx, v_block->leader_consen_stat->succ_num);
    SetMemberConsensusStat(v_block->leader_idx, v_block->leader_consen_stat);

    std::string ret;
    auto all_consen_stats = GetAllConsensusStats();
    for (uint32_t idx = 0; idx < all_consen_stats.size(); idx++) {
        ret += std::to_string(idx) + ": " + std::to_string(all_consen_stats[idx]->succ_num) + ", ";
    }
    ZJC_DEBUG("pool: %d get all stat: %s", ret.c_str());
    
    
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

