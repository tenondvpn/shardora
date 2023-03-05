#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cassert>

#include "common/encode.h"
#include "common/lof.h"
#include "common/log.h"
#include "common/string_utils.h"
#include "common/user_property_key_define.h"
#include "common/utils.h"
#include "db/db.h"
#include "pools/tx_utils.h"
#include "protos/block.pb.h"

#define BLOCK_DEBUG(fmt, ...) ZJC_DEBUG("[block]" fmt, ## __VA_ARGS__)
#define BLOCK_INFO(fmt, ...) ZJC_INFO("[block]" fmt, ## __VA_ARGS__)
#define BLOCK_WARN(fmt, ...) ZJC_WARN("[block]" fmt, ## __VA_ARGS__)
#define BLOCK_ERROR(fmt, ...) ZJC_ERROR("[block]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace block {

enum BlockErrorCode {
    kBlockSuccess = 0,
    kBlockError = 1,
    kBlockDbNotExists = 2,
    kBlockDbDataInvalid = 3,
    kBlockAddressNotExists = 4,
};

enum AddressType {
    kNormalAddress = 0,
    kContractAddress = 1,
};

struct HeightItem {
    uint64_t height;
    std::string hash;
};

static const uint32_t kStatisticMaxCount = 3u;

struct ToTxsItem {
    ToTxsItem() : tx_ptr(nullptr), tx_count(0), in_consensus(false) {}
    pools::TxItemPtr tx_ptr;
    std::string to_txs_hash;
    uint32_t tx_count;
    bool in_consensus;
};

struct StatisticElectItem {
    StatisticElectItem() : elect_height(0) {
        memset(succ_tx_count, 0, sizeof(succ_tx_count));
    }

    void Clear() {
        elect_height = 0;
        memset(succ_tx_count, 0, sizeof(succ_tx_count));
        leader_lof_map.clear();
    }

    uint64_t elect_height{ 0 };
    uint32_t succ_tx_count[common::kEachShardMaxNodeCount];
    std::unordered_map<int32_t, std::shared_ptr<common::Point>> leader_lof_map;
    std::mutex leader_lof_map_mutex;
};

typedef std::shared_ptr<StatisticElectItem> StatisticElectItemPtr;

struct StatisticItem {
    StatisticItem() {
        for (uint32_t i = 0; i < kStatisticMaxCount; ++i) {
            elect_items[i] = std::make_shared<StatisticElectItem>();
        }
    }

    void Clear() {
        for (uint32_t i = 0; i < kStatisticMaxCount; ++i) {
            elect_items[i]->Clear();
        }

        all_tx_count = 0;
        tmblock_height = 0;
        added_height.clear();
    }

    StatisticElectItemPtr elect_items[kStatisticMaxCount];
    uint32_t all_tx_count{ 0 };
    std::unordered_set<uint64_t> added_height;
    uint64_t tmblock_height{ 0 };
};

typedef std::shared_ptr<block::protobuf::Block> BlockPtr;

struct BlockToDbItem {
    BlockToDbItem(BlockPtr& bptr) : block_ptr(bptr) {}
    BlockPtr block_ptr;
    db::DbWriteBach db_batch;
    bool is_kv_synced{ false };
};

typedef std::shared_ptr<BlockToDbItem> BlockToDbItemPtr;

static const uint32_t kUnicastAddressLength = 20u;
static const std::string kLastBlockHashPrefix("last_block_hash_pre_");
static const std::string kFieldContractOwner = common::Encode::HexDecode(
    "0000000000000000000000000000000000000000000000000000000000000000");
static const std::string kFieldFullAddress = common::Encode::HexDecode(
    "0000000000000000000000000000000000000000000000000000000000001000");

static inline std::string GetLastBlockHash(uint32_t network_id, uint32_t pool_idx) {
    return (kLastBlockHashPrefix + std::to_string(network_id) + "_" + std::to_string(pool_idx));
}

static inline std::string UnicastAddress(const std::string& src_address) {
    assert(src_address.size() >= kUnicastAddressLength);
    return src_address.substr(
        src_address.size() - kUnicastAddressLength,
        kUnicastAddressLength);
}

inline static std::string StorageDbKey(const std::string& account_id,  const std::string& key) {
    return account_id + "_vms_" + key;
}

inline static bool IsPoolBaseAddress(const std::string& address) {
    if (address.substr(2, 16) == common::kStatisticFromAddressMidllefixDecode) {
        return true;
    }

    return false;
}

inline static std::string GetElectBlsMembersKey(uint64_t height, uint32_t shard_id) {
    return std::string("__EM_") +
        std::to_string(shard_id) + "_" +
        std::to_string(height);
}

}  // namespace block

}  // namespace zjchain
