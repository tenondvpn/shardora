#include <common/global_info.h>
#include <common/hash.h>
#include <common/node_members.h>
#include <common/time_utils.h>
#include <consensus/hotstuff/leader_rotation.h>

namespace shardora {

namespace consensus {

LeaderRotation::LeaderRotation(const std::shared_ptr<ViewBlockChain>& chain, const std::shared_ptr<ElectInfo>& elect_info) :
    chain_(chain), elect_info_(elect_info) {}

LeaderRotation::~LeaderRotation() {}

common::BftMemberPtr LeaderRotation::GetLeader() {
    auto committedBlock = chain_->LatestCommittedBlock();
    auto qc = committedBlock->qc;
    uint64_t random_hash = common::Hash::Hash64(qc->Serialize() +
        std::to_string(common::TimeUtils::TimestampMs() / (30 * 1000)));

    auto idx = qc->participants[random_hash % qc->participants.size()];
    // check if idx is one of members_ in case that a new epoch starts
    for (uint32_t i = 0; i < Members()->size(); i++) {
        if ((*Members())[i]->index == idx) {
            return (*Members())[i];
        }
    }
    
    return (*Members())[random_hash % Members()->size()];
}

} // namespace hotstuff

} // namespace shardora

