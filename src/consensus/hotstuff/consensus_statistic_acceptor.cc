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

Status ConsensusStatAcceptor::Accept(std::shared_ptr<ViewBlock> &v_block) {
    // 具体当前分支上未提交的 leader 的分数，计算出 v_block 中 leader 的分数    
    if (!v_block || !view_block_chain_->LatestCommittedBlock()) {
        return Status::kError;
    }

    auto elect_item = elect_info_->GetElectItem(
            common::GlobalInfo::Instance()->network_id(), v_block->ElectHeight());
    if (!elect_item || !elect_item->IsValid()) {
        return Status::kError;
    }
    v_block->leader_consen_stat = elect_item->GetMemberConsensusStat(v_block->leader_idx);
    v_block->leader_consen_stat->succ_num++;
    
    // auto current = v_block;
    // uint32_t n = 0;
    // 理论上 3 个之前的块就是 LatestCommittedBlock，除非还没有同步过来
    // 为了防止过多循环，限制循环次数不超过 3 次
    // while (current->view > view_block_chain_->LatestCommittedBlock()->view && n < 3) {
    //     current = view_block_chain_->QCRef(current);
    //     if (!current) {
    //         return Status::kError;
    //     }
    //     if (current->leader_idx == v_block->leader_idx) {
    //         v_block->leader_consen_stat = std::make_shared<MemberConsensusStat>(
    //                 current->leader_consen_stat->succ_num+1,
    //                 current->leader_consen_stat->fail_num);
    //         break;
    //     }
    //     ++n;
    // }
    
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

    auto elect_item = elect_info_->GetElectItem(
            common::GlobalInfo::Instance()->network_id(), v_block->ElectHeight());
    if (!elect_item) {
        return Status::kError;
    }
    elect_item->SetMemberConsensusStat(v_block->leader_idx, v_block->leader_consen_stat);
    leader_last_commit_view_map_[v_block->leader_idx] = v_block->view;
    
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

