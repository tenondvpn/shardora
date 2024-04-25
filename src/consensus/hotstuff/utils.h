#pragma once

#include <common/hash.h>
#include <protos/block.pb.h>
#include <protos/prefix_db.h>

namespace shardora {

namespace hotstuff {

std::string GetTxMessageHash(const block::protobuf::BlockTx& tx_info);
std::string GetBlockHash(const block::protobuf::Block &block);

} // namespace consensus

} // namespace shardora

