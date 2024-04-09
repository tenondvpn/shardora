#include <common/global_info.h>
#include <common/hash.h>
#include <common/node_members.h>
#include <common/time_utils.h>
#include <consensus/hotstuff/leader_rotation.h>

namespace shardora {

namespace consensus {

LeaderRotation::LeaderRotation(const std::shared_ptr<ViewBlockChain>& chain) : chain_(chain) {}

LeaderRotation::~LeaderRotation() {}

common::BftMemberPtr LeaderRotation::GetLeader() {
    auto committedBlock = chain_->LatestCommittedBlock();
    auto qc = committedBlock->qc;
    uint64_t random_hash = common::Hash::Hash64(qc->Serialize());
    random_hash += common::TimeUtils::TimestampMs() / (30 * 1000);

    auto idx = qc->participants[random_hash % qc->participants.size()];
    // check if idx is one of members_ in case that a new epoch starts
    for (uint32_t i = 0; i < members_->size(); i++) {
        if ((*members_)[i]->index == idx) {
            return (*members_)[i];
        }
    }
    
    return (*members_)[random_hash % members_->size()];
}

void LeaderRotation::OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height, const common::MembersPtr& members) {
    if (sharding_id != common::GlobalInfo::Instance()->network_id()) {
        return;
    }

    if (latest_elect_height_ >= elect_height) {
        return;
    }

    members_ = members;

    for (uint32_t i = 0; i < members->size(); i++) {
        if ((*members)[i]->id == security_ptr_->GetAddress()) {
            local_member_idx_ = i;
            break;
        }
    }
}

}

} // namespace shardora

