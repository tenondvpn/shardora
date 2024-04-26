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

LeaderRotation::LeaderRotation(const std::shared_ptr<ViewBlockChain>& chain, const std::shared_ptr<ElectInfo>& elect_info) :
    chain_(chain), elect_info_(elect_info) {}

LeaderRotation::~LeaderRotation() {}

common::BftMemberPtr LeaderRotation::GetLeader() {
    auto committedBlock = chain_->LatestCommittedBlock();
    // 对于非种子节点可能启动时没有 committedblock, 需要等同步
    auto qc = GetQCWrappedByGenesis();
    if (committedBlock) {
        qc = committedBlock->qc;
    }
    
    uint64_t random_hash = common::Hash::Hash64(qc->Serialize() +
        std::to_string(common::TimeUtils::TimestampSeconds() / 6000000));

    if (Members()->empty()) {
        return nullptr;
    }
        
    auto leader = (*Members())[random_hash % Members()->size()];
    if (leader->public_ip == 0 || leader->public_port == 0) {
        // 刷新 members 的 ip port
        elect_info_->RefreshMemberAddrs();
    }

    ZJC_DEBUG("Leader is %d, ip: %s, port: %d",
        leader->index,
        common::Uint32ToIp(leader->public_ip).c_str(), leader->public_port);
    return leader;
}

} // namespace hotstuff

} // namespace shardora

