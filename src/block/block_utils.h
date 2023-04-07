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
    ToTxsItem() : tx_ptr(nullptr), tx_count(0) {}
    pools::TxItemPtr tx_ptr;
    std::string to_txs_hash;
    uint32_t tx_count;
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
    BlockToDbItem(BlockPtr& bptr, const std::shared_ptr<db::DbWriteBatch>& batch)
        : block_ptr(bptr), db_batch(batch) {}
    BlockPtr block_ptr;
    std::shared_ptr<db::DbWriteBatch> db_batch;
};

typedef std::shared_ptr<BlockToDbItem> BlockToDbItemPtr;

typedef std::function<void(
    uint8_t thread_idx,
    const std::shared_ptr<block::protobuf::Block>& block,
    db::DbWriteBatch& db_batch)> DbBlockCallback;

}  // namespace block

}  // namespace zjchain
