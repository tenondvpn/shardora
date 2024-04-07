#include <common/global_info.h>
#include <common/hash.h>
#include <common/time_utils.h>
#include <consensus/hotstuff/leader_rotation.h>


namespace shardora {

namespace consensus {

LeaderRotation::LeaderRotation(const std::shared_ptr<ViewBlockChain>& chain) : chain_(chain) {}

LeaderRotation::~LeaderRotation() {}

uint32_t LeaderRotation::GetLeaderIdx() {
    auto committedBlock = chain_->LatestCommittedBlock();
    auto qc = committedBlock->qc;
    uint64_t random_hash = common::Hash::Hash64(qc->Serialize());
    random_hash += common::TimeUtils::TimestampMs() / (30 * 1000);

    auto idx = qc->participants[random_hash % qc->participants.size()];
    // check if idx is one of members_ in case that a new epoch starts
    for (uint32_t i = 0; i < members_->size(); i++) {
        if ((*members_)[i]->index == idx) {
            return idx;
        }
    }
    
    return random_hash % members_->size();
}

void LeaderRotation::OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height, const common::MembersPtr& members) {
    if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    if (latest_elect_height_ >= elect_height) {
        return;
    }

    members_ = members;
}

}

} // namespace shardora

