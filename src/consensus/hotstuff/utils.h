#pragma once

#include <common/hash.h>
#include <protos/block.pb.h>
#include <protos/prefix_db.h>

namespace shardora {

namespace hotstuff {

std::string GetTxMessageHash(
    const block::protobuf::BlockTx& tx_info, 
    const std::string& phash);
std::string GetBlockHash(const view_block::protobuf::ViewBlockItem &view_block);

} // namespace consensus

} // namespace shardora

