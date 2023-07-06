#include "consensus/zbft/zbft_utils.h"

#include "consensus/consensus_utils.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"

namespace zjchain {

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

std::string GetTxMessageHash(const block::protobuf::BlockTx& tx_info) {
    std::string message;
    message.reserve(tx_info.ByteSizeLong());
    message.append(tx_info.gid());
    message.append(tx_info.from());
    message.append(tx_info.to());
    uint64_t amount = tx_info.amount();
    message.append(std::string((char*)&amount, sizeof(amount)));
    uint64_t gas_limit = tx_info.gas_limit();
    message.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
    uint64_t gas_price = tx_info.gas_price();
    message.append(std::string((char*)&gas_price, sizeof(gas_price)));
    uint32_t step = tx_info.step();
    message.append(std::string((char*)&step, sizeof(step)));
    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        message.append(tx_info.storages(i).key());
        message.append(tx_info.storages(i).val_hash());
    }

    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        ZJC_DEBUG("amount: %lu, gas_limit: %lu, gas_price: %lu, step: %u, key: %s, %s, val: %s, block tx hash: %s, message: %s",
            amount, gas_limit, gas_price, step,
            common::Encode::HexEncode(tx_info.storages(i).key()).c_str(),
            tx_info.storages(i).key().c_str(),
            common::Encode::HexEncode(tx_info.storages(i).val_hash()).c_str(),
            common::Encode::HexEncode(common::Hash::keccak256(message)).c_str(),
            common::Encode::HexEncode(message).c_str());
    }

    return common::Hash::keccak256(message);
}

std::string GetBlockHash(const block::protobuf::Block& block) {
    std::string msg;
    msg.reserve(block.tx_list_size() * 32 + 256);
    for (int32_t i = 0; i < block.tx_list_size(); i++) {
        msg.append(GetTxMessageHash(block.tx_list(i)));
    }

    msg.append(block.prehash());
    uint32_t pool_idx = block.pool_index();
    msg.append((char*)&pool_idx, sizeof(pool_idx));
    uint32_t sharding_id = block.network_id();
    msg.append((char*)&sharding_id, sizeof(sharding_id));
    uint64_t vss_random = block.consistency_random();
    msg.append((char*)&vss_random, sizeof(vss_random));
    uint64_t height = block.height();
    msg.append((char*)&height, sizeof(height));
    uint64_t timeblock_height = block.timeblock_height();
    msg.append((char*)&timeblock_height, sizeof(timeblock_height));
    uint64_t elect_height = block.electblock_height();
    msg.append((char*)&elect_height, sizeof(elect_height));
    uint32_t leader_idx = block.leader_index();
    msg.append((char*)&leader_idx, sizeof(leader_idx));
    bool is_cross_block = block.is_cross_block();
    msg.append((char*)&is_cross_block, sizeof(is_cross_block));

    ZJC_DEBUG("block.prehash(): %s, height: %lu,pool_idx: %u, sharding_id: %u, vss_random: %lu, "
        "timeblock_height: %lu, elect_height: %lu, leader_idx: %u, get block hash: %s, %s, "
        "is_cross_block: %d",
        common::Encode::HexEncode(block.prehash()).c_str(),
        height,
        pool_idx,
        sharding_id,
        vss_random,
        timeblock_height,
        elect_height,
        leader_idx,
        common::Encode::HexEncode(common::Hash::keccak256(msg)).c_str(),
        common::Encode::HexEncode(msg).c_str(),
        is_cross_block);

    return common::Hash::keccak256(msg);
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

}  //namespace zjchain
