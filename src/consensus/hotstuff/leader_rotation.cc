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
    pool_idx_(pool_idx), chain_(chain), elect_info_(elect_info) {}

LeaderRotation::~LeaderRotation() {}

common::BftMemberPtr LeaderRotation::GetLeader() {
    // 此处选择 CommitBlock 包含的 QC 作为随机种子，也可以选择 CommitQC 或者 LockedQC
    // 但不能选择 HighQC（由于同步延迟无法保证某时刻 HighQC 大多数节点相同）
    auto committedBlock = chain_->LatestCommittedBlock();
    // 对于非种子节点可能启动时没有 committedblock, 需要等同步
    auto qc = GetQCWrappedByGenesis();
    if (committedBlock) {
        qc = committedBlock->qc;
    }
    // TODO 30s 轮换会导致 Leader 不一致而卡住共识
    uint64_t random_hash = common::Hash::Hash64(qc->Serialize() +
        std::to_string(common::TimeUtils::TimestampSeconds() / 300000));

    if (Members()->empty()) {
        return nullptr;
    }
        
    auto leader = (*Members())[random_hash % Members()->size()];
    if (leader->public_ip == 0 || leader->public_port == 0) {
        // 刷新 members 的 ip port
        elect_info_->RefreshMemberAddrs();
    }

    ZJC_DEBUG("Leader pool: %d, is %d, ip: %s, port: %d, qc view: %lu",
        pool_idx_,
        leader->index,
        common::Uint32ToIp(leader->public_ip).c_str(), leader->public_port,
        qc->view);
    return leader;
}

} // namespace hotstuff

} // namespace shardora
