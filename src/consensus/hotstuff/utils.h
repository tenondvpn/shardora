#pragma once

#include <memory>
#include <tuple>

#include "common/hash.h"
#include "protos/block.pb.h"

namespace shardora {

namespace protos {
    class PrefixDb;
}

namespace hotstuff {

class ViewBlockChain;
enum ChainType : int32_t {
    kInvalidChain = -1,
    kLocalChain = 0,
    kCrossRootChian = 1,
    kCrossShardingChain = 2,
};

struct BlockViewKey {
    uint32_t network_id;
    uint32_t pool_index;
    uint64_t view;
    BlockViewKey(uint32_t network_id,
        uint32_t pool_index,
        uint64_t view) : network_id(network_id), pool_index(pool_index), view(view) {}

    bool operator==(const BlockViewKey& other) const {
        return network_id == other.network_id &&
               pool_index == other.pool_index &&
               view == other.view;
    }

    bool operator<(const BlockViewKey& other) const {
        return std::tie(network_id, pool_index, view) <
               std::tie(other.network_id, other.pool_index, other.view);
    }
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
bool BlockViewCommited(
    std::shared_ptr<protos::PrefixDb> prefix_db, 
    uint32_t network_id,
    uint64_t view);
bool ViewBlockIsCheckedParentHash(
    std::shared_ptr<protos::PrefixDb> prefix_db, 
    const std::string& hash);

} // namespace consensus

} // namespace shardora

namespace std {
    template <>
    struct hash<shardora::consensus::BlockViewKey> {
        std::size_t operator()(const shardora::consensus::BlockViewKey& k) const {
            std::size_t seed = 0;
            auto hash_combine = [&seed](auto value) {
                seed ^= std::hash<decltype(value)>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };

            hash_combine(k.network_id);
            hash_combine(k.pool_index);
            hash_combine(k.view);
            return seed;
        }
    };
}
