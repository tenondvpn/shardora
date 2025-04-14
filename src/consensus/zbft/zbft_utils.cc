#include "consensus/zbft/zbft_utils.h"

#include "consensus/consensus_utils.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"

namespace shardora {

namespace consensus {

std::string StatusToString(uint32_t status) {
    switch (status) {
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
    default:
        return "unknown";
    }
}

std::string GetTxValueProtoHash(const std::string& key, const std::string& value) {
    return "";
}


std::string GetCommitedBlockHash(const std::string& prepare_hash) {
    auto tmp_hash = prepare_hash;
    bool is_commited_block = true;
    tmp_hash.append((char*)&is_commited_block, sizeof(is_commited_block));
    tmp_hash = common::Hash::keccak256(tmp_hash);
    return tmp_hash;
}

uint32_t NewAccountGetNetworkId(const std::string& addr) {
    return 3;
    // return static_cast<uint32_t>((common::Hash::Hash64(addr) *
    //     vss::VssManager::Instance()->EpochRandom()) %
    //     common::GlobalInfo::Instance()->consensus_shard_count()) +
    //     network::kConsensusShardBeginNetworkId;
}

bool IsRootSingleBlockTx(uint32_t tx_type) {
    if (tx_type == common::kConsensusRootElectShard ||
            tx_type == common::kConsensusRootTimeBlock) {
        return true;
    }

    return false;
}

bool IsShardSingleBlockTx(uint32_t tx_type) {
    return IsRootSingleBlockTx(tx_type);
}

}  // namespace consensus

}  //namespace shardora
