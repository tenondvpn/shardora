#pragma once

#include <memory>
#include <tuple>
#include <unordered_map>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include "common/hash.h"
#include "protos/address.pb.h"
#include "protos/block.pb.h"
#include "protos/view_block.pb.h"

namespace shardora {

namespace protos {
    class PrefixDb;
}

namespace pools {
    class TxPoolManager;
}

namespace hotstuff {

class ViewBlockChain;
// Keep in sync with hotstuff_utils.h; defined here to avoid include cycle (types.h -> utils.h -> hotstuff_utils.h -> types.h).
using BalanceAndNonceMap = std::unordered_map<std::string, std::shared_ptr<address::protobuf::AddressInfo>>;
enum ChainType : int32_t {
    kInvalidChain = -1,
    kLocalChain = 0,
    kCrossRootChian = 1,
    kCrossShardingChain = 2,
};

/** Chain id for constexpr contexts; must match `kGlobalChainId` in utils.cc. */
inline constexpr uint64_t kGlobalChainIdValue = 3355103125ULL;
extern const uint64_t kGlobalChainId;

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

using BlockHeightKey = BlockViewKey;

std::string GetTxMessageHash(
    const block::protobuf::BlockTx& tx_info, 
    const std::string& phash);
std::string GetBlockHash(const view_block::protobuf::ViewBlockItem &view_block);
int CheckTransactionValid(
    const std::string& parent_hash, 
    std::shared_ptr<ViewBlockChain> view_block_chain,
    std::shared_ptr<pools::TxPoolManager> pool_mgr,
    const address::protobuf::AddressInfo& addr_info, 
    const pools::protobuf::TxMessage& tx_info,
    uint64_t* now_nonce,
    const BalanceAndNonceMap* merged_balance_map = nullptr);
bool BlockHeightCommited(
    std::shared_ptr<protos::PrefixDb> prefix_db, 
    uint32_t network_id,
    uint32_t pool_index, 
    uint64_t view);
bool ViewBlockIsCheckedParentHash(
    std::shared_ptr<protos::PrefixDb> prefix_db, 
    const std::string& hash);

} // namespace hotstuff

} // namespace shardora
/**
 * @brief Universal Deterministic Serialization Function
 * @param message The Protobuf message object to be serialized
 * @return std::string The serialized byte string, or an empty string if failed
 */
template <typename T>
std::string SerializeDeterministic(const T& message) {
    // 1. Pre-calculate the total byte size
    // Crucial for multi-threaded safety: if the message is modified between 
    // size calculation and writing, Protobuf will trigger a consistency crash.
    size_t size = message.ByteSizeLong();
    
    // Optional: Check if the message is initialized (all required fields present)
    if (size == 0 && !message.IsInitialized()) {
        // Log or handle uninitialized message if necessary
    }

    std::string output;
    output.reserve(size); // Pre-allocate memory to optimize performance

    {
        google::protobuf::io::StringOutputStream string_stream(&output);
        google::protobuf::io::CodedOutputStream coded_output(&string_stream);

        // Enable Deterministic Serialization: Ensures identical messages 
        // result in identical byte sequences across different platforms/instances.
        // Critical for hash calculations and distributed signature verification.
        coded_output.SetSerializationDeterministic(true);

        // Using SerializePartialToCodedStream allows serialization even if 
        // some 'required' fields are missing. Use SerializeToCodedStream for strict checks.
        if (!message.SerializePartialToCodedStream(&coded_output)) {
            return ""; 
        }
        
        // The buffer is flushed to the StringOutputStream when coded_output 
        // goes out of scope or Trim() is called.
    }

    return output;
}

namespace std {
    template <>
    struct hash<shardora::hotstuff::BlockViewKey> {
        std::size_t operator()(const shardora::hotstuff::BlockViewKey& k) const {
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
