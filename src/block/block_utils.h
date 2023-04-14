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
