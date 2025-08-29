#include <common/encode.h>
#include <common/global_info.h>
#include <common/hash.h>
#include <common/log.h>
#include <common/node_members.h>
#include <common/time_utils.h>
#include <common/utils.h>
#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>

namespace shardora {

namespace hotstuff {

LeaderRotation::LeaderRotation(
        const uint32_t& pool_idx,
        const std::shared_ptr<ViewBlockChain>& chain,
        const std::shared_ptr<ElectInfo>& elect_info) :
    pool_idx_(pool_idx), chain_(chain), elect_info_(elect_info) {
    SetExpectedLeader(GetLeader());
}

LeaderRotation::~LeaderRotation() {}

common::BftMemberPtr LeaderRotation::GetLeader() {
    // 此处选择 CommitBlock 包含的 QC 作为随机种子，也可以选择 CommitQC 或者 LockedQC
    // 但不能选择 HighQC（由于同步延迟无法保证某时刻 HighQC 大多数节点相同）
    auto committedBlock = chain_->LatestCommittedBlock();
    
    // 对于非种子节点可能启动时没有 committedblock, 需要等同步
    QC qc;
    const QC* qc_ptr = &qc;
    GetQCWrappedByGenesis(pool_idx_, &qc);
    if (committedBlock) {
        ZJC_DEBUG("pool: %d, get leader success get latest commit block view: %lu, %s",
            pool_idx_, committedBlock->view(),
            common::Encode::HexEncode(committedBlock->hash()).c_str());
        qc_ptr = &committedBlock->self_commit_qc();
    } else {
        ZJC_DEBUG("pool: %d, committed block is empty", pool_idx_);
    }

    auto qc_hash = GetQCMsgHash(*qc_ptr);
    uint32_t now_time_num = common::TimeUtils::TimestampSeconds() / TIME_EPOCH_TO_CHANGE_LEADER_S;
    uint64_t random_hash = common::Hash::Hash64(qc_hash + std::to_string(now_time_num) + extra_nonce_);
    if (Members(common::GlobalInfo::Instance()->network_id())->empty()) {
        return nullptr;
    }
    
    auto leader = getLeaderByRate(static_cast<uint64_t>(random_hash));
    if (leader->public_ip == 0 || leader->public_port == 0) {
        // 刷新 members 的 ip port
        elect_info_->RefreshMemberAddrs(common::GlobalInfo::Instance()->network_id());
        // ZJC_DEBUG("refresh Leader pool: %d, is %d, id: %s, ip: %s, port: %d, qc view: %lu",
        //     pool_idx_,
        //     leader->index,
        //     common::Encode::HexEncode(leader->id).c_str(),
        //     common::Uint32ToIp(leader->public_ip).c_str(), leader->public_port,
        //     qc_ptr->view());
    }

    ZJC_DEBUG("pool: %d Leader is %d, local: %d, id: %s, qc_hash: %s, ip: %s, port: %d, "
        "qc view: %lu, time num: %lu, extra_nonce: %s",
        pool_idx_,
        leader->index,
        GetLocalMemberIdx(),
        common::Encode::HexEncode(leader->id).c_str(),
        common::Encode::HexEncode(qc_hash).c_str(),
        common::Uint32ToIp(leader->public_ip).c_str(), leader->public_port,
        qc_ptr->view(),
        now_time_num,
        extra_nonce_.c_str());
    return leader;
}

common::BftMemberPtr LeaderRotation::getLeaderByRate(uint64_t random_hash) {
    // auto consensus_stat = elect_info_->GetElectItemWithShardingId(
    //         common::GlobalInfo::Instance()->network_id())->consensus_stat(pool_idx_);
    
    // uint32_t total_score = consensus_stat->TotalSuccNum();
    // if (total_score == 0) {
    //     return getLeaderByRandom(random_hash);        
    // }
    
    // int32_t random_value = random_hash % total_score;
    
    // auto all_consen_stats = consensus_stat->GetAllConsensusStats();
    // int accumulated_score = 0;
    // for (size_t leader_idx = 0; leader_idx < all_consen_stats.size(); ++leader_idx) {
    //     accumulated_score += all_consen_stats[leader_idx]->succ_num;
    //     if (random_value < accumulated_score) {
    //         return (*Members(common::GlobalInfo::Instance()->network_id()))[leader_idx];
    //     }
    // }    

    return (*Members(common::GlobalInfo::Instance()->network_id()))[0];
}

common::BftMemberPtr LeaderRotation::getLeaderByRandom(uint64_t random_hash) {
    auto leader_idx = random_hash % Members(common::GlobalInfo::Instance()->network_id())->size();
    return (*Members(common::GlobalInfo::Instance()->network_id()))[leader_idx];
}

} // namespace hotstuff

} // namespace shardora

