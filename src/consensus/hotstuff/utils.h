#pragma once

#include <common/hash.h>
#include <protos/block.pb.h>
#include <protos/prefix_db.h>

namespace shardora {

namespace hotstuff {

class ViewBlockChain;
enum ChainType : int32_t {
    kInvalidChain = -1,
    kLocalChain = 0,
    kCrossRootChian = 1,
    kCrossShardingChain = 2,
};

std::string GetTxMessageHash(
    const block::protobuf::BlockTx& tx_info, 
    const std::string& phash);
std::string GetBlockHash(const view_block::protobuf::ViewBlockItem &view_block);
int CheckTransactionValid(
    const std::string& parent_hash, 
    std::shared_ptr<ViewBlockChain> view_block_chain,
    const address::protobuf::AddressInfo& addr_info, 
    pools::protobuf::TxMessage& tx_info,
    uint64_t* now_nonce);
bool view_commited(
    std::shared_ptr<protos::PrefixDb> prefix_db, 
    uint32_t network_id, View view);
bool ViewBlockIsCheckedParentHash(
    std::shared_ptr<protos::PrefixDb> prefix_db, 
    const std::string& hash);

} // namespace consensus

} // namespace shardora

