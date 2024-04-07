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
    uint64_t random_hash = common::Hash::Hash64(committedBlock->qc->Serialize());
    random_hash += common::TimeUtils::TimestampMs() / (30 * 1000);
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

