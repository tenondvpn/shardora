#pragma once

#include <common/hash.h>
#include <protos/block.pb.h>

namespace shardora {

namespace consensus {

std::string GetTxMessageHash(const block::protobuf::BlockTx& tx_info);
std::string GetBlockHash(const block::protobuf::Block& block);

} // namespace consensus

} // namespace shardora

