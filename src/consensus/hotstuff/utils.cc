#include "consensus/hotstuff/utils.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <consensus/hotstuff/types.h>

namespace shardora {

namespace hotstuff {

std::string GetBlockHash(const view_block::protobuf::ViewBlockItem &view_block) {
    std::string serialized;
    google::protobuf::io::StringOutputStream string_stream(&serialized);
    google::protobuf::io::CodedOutputStream coded_output(&string_stream);
    coded_output.SetSerializationDeterministic(true); 
    auto& block = view_block.block_info();
    if (!block.SerializePartialToCodedStream(&coded_output)) {
        return "";
    }

    coded_output.Trim();
    uint32_t sharding_id = view_block.qc().network_id();
    serialized.append((char*)&sharding_id, sizeof(sharding_id));
    uint32_t pool_index = view_block.qc().pool_index();
    serialized.append((char*)&pool_index, sizeof(pool_index));
    serialized.append(view_block.parent_hash());
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
