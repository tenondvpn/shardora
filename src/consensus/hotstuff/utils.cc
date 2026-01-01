#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/utils.h>

namespace shardora {

namespace hotstuff {

std::string GetTxMessageHash(const block::protobuf::BlockTx& tx_info, const std::string& phash) {
    std::string serialized;
    google::protobuf::io::StringOutputStream output(&serialized);
    google::protobuf::io::CodedOutputStream coded_output(&output);
    tx_info.SerializePartialToString(&serialized);
    auto hash = common::Hash::keccak256(serialized);
    return hash;
}

std::string GetBlockHash(const view_block::protobuf::ViewBlockItem &view_block) {
    auto& block = view_block.block_info();
    std::string serialized;
    google::protobuf::io::StringOutputStream output(&serialized);
    google::protobuf::io::CodedOutputStream coded_output(&output);
    block.SerializePartialToString(&serialized);
    auto hash = common::Hash::keccak256(serialized);

    SHARDORA_DEBUG("get block hash: %s, sharding_id: %u, pool_index: %u, "
        "phash: %s, vss_random: %lu, height: %lu, "
        "timeblock_height: %lu, timestamp: %lu, msg: %s",
        common::Encode::HexEncode(hash).c_str(),
        view_block.qc().network_id(), 
        view_block.qc().pool_index(), 
        common::Encode::HexEncode(view_block.parent_hash()).c_str(), 
        block.consistency_random(), 
        block.height(), 
        block.timeblock_height(), 
        block.timestamp(),
        ProtobufToJson(block).c_str());

    return hash;
}

} // namespace consensus

} // namespace shardora
