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
    auto qc = GetQCWrappedByGenesis();
    if (committedBlock) {
        ZJC_DEBUG("pool: %d, get leader success get latest commit block view: %lu, %s",
            pool_idx_, committedBlock->view,
            common::Encode::HexEncode(committedBlock->hash).c_str());
        qc = committedBlock->qc;
    } else {
        ZJC_DEBUG("pool: %d, committed block is empty", pool_idx_);
    }

    uint32_t now_time_num = common::TimeUtils::TimestampSeconds() / TIME_EPOCH_TO_CHANGE_LEADER_S;
    uint64_t random_hash = common::Hash::Hash64(qc->Serialize() + std::to_string(now_time_num));

    if (Members()->empty()) {
        return nullptr;
    }
    
    auto leader_idx = random_hash % Members()->size();
    // TODO(test)
    // leader_idx = 0;
    auto leader = (*Members())[leader_idx];
    if (leader->public_ip == 0 || leader->public_port == 0) {
        // 刷新 members 的 ip port
        elect_info_->RefreshMemberAddrs();
        ZJC_DEBUG("refresh Leader pool: %d, is %d, id: %s, ip: %s, port: %d, qc view: %lu",
            pool_idx_,
            leader->index,
            common::Encode::HexEncode(leader->id).c_str(),
            common::Uint32ToIp(leader->public_ip).c_str(), leader->public_port,
            qc->view);
    }

    ZJC_DEBUG("Leader pool: %d, is %d, local: %d, id: %s, ip: %s, port: %d, qc view: %lu, time num: %lu",
        pool_idx_,
        leader->index,
        GetLocalMemberIdx(),
        common::Encode::HexEncode(leader->id).c_str(),
        common::Uint32ToIp(leader->public_ip).c_str(), leader->public_port,
        qc->view,
        now_time_num);
    return leader;
}

} // namespace hotstuff

} // namespace shardora

