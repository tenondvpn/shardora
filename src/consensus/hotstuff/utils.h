#pragma once

#include <common/hash.h>
#include <protos/block.pb.h>
#include <protos/prefix_db.h>

namespace shardora {

namespace hotstuff {

enum ChainType : int32_t {
    kInvalidChain = -1,
    kLocalChain = 0,
    kCrossRootChian = 1,
    kCrossShardingChain = 2,
};

using SyncPoolFn = std::function<void(const uint32_t &, const int32_t&)>;
std::string GetTxMessageHash(
    const block::protobuf::BlockTx& tx_info, 
    const std::string& phash);
std::string GetBlockHash(const view_block::protobuf::ViewBlockItem &view_block);

} // namespace consensus

} // namespace shardora

