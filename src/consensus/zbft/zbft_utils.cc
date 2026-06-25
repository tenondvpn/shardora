#include "consensus/zbft/zbft_utils.h"

#include "common/hash.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"

namespace shardora {
namespace consensus {

std::string StatusToString(uint32_t status) {
    switch (static_cast<BftStatus>(status)) {
    case kConsensusInit:
        return "bft_init";
    case kConsensusPrepare:
        return "bft_prepare";
    case kConsensusPreCommit:
        return "bft_precommit";
    case kConsensusCommit:
        return "bft_commit";
    case kConsensusCommited:
        return "bft_success";
    case kConsensusToTxInit:
        return "bft_to_tx_init";
    case kConsensusRootBlock:
        return "bft_root_block";
    case kConsensusCallContract:
        return "bft_call_contract";
    case kConsensusStepTimeout:
        return "bft_step_timeout";
    case kConsensusSyncBlock:
        return "bft_sync_block";
    case kConsensusFailed:
        return "bft_failed";
    case kConsensusWaitingBackup:
        return "bft_waiting_backup";
    case kConsensusOppose:
        return "bft_oppose";
    case kConsensusAgree:
        return "bft_agree";
    case kConsensusHandled:
        return "bft_handled";
    case kConsensusReChallenge:
        return "bft_re_challenge";
    case kConsensusLeaderWaitingBlock:
        return "bft_leader_waiting_block";
    default:
        return "unknown";
    }
}

std::string GetTxValueProtoHash(const std::string& key, const std::string& value) {
    (void)key;
    (void)value;
    return "";
}

std::string GetCommitedBlockHash(const std::string& prepare_hash) {
    return common::Hash::keccak256(prepare_hash + "commited");
}

uint32_t NewAccountGetNetworkId(const std::string& addr) {
    (void)addr;
    return static_cast<uint32_t>(common::kConsensusCreateAcount);
}

bool IsRootSingleBlockTx(uint32_t tx_type) {
    return tx_type == common::kConsensusRootElectShard ||
            tx_type == common::kConsensusRootTimeBlock;
}

bool IsShardSingleBlockTx(uint32_t tx_type) {
    return IsRootSingleBlockTx(tx_type);
}

}  // namespace consensus
}  // namespace shardora
