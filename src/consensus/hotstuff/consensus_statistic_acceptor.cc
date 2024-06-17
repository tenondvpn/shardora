#include <consensus/hotstuff/consensus_statistic_acceptor.h>
#include <consensus/hotstuff/types.h>

namespace shardora {

namespace hotstuff {

ConsensusStatAcceptor::ConsensusStatAcceptor(
        uint32_t pool_idx,
        std::shared_ptr<ElectInfo> elect_info) :
    pool_idx_(pool_idx), elect_info_(elect_info) {}

ConsensusStatAcceptor::~ConsensusStatAcceptor() {}

Status ConsensusStatAcceptor::Accept(std::shared_ptr<ViewBlock> &v_block) {
    return Status::kSuccess;
}

Status ConsensusStatAcceptor::Commit(const std::shared_ptr<ViewBlock> &v_block) {
    return Status::kSuccess;
}

} // namespace hotstuff

} // namespace shardora

