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
#include "common/utils.h"
#include "db/db.h"
#include "network/network_utils.h"
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

static const std::string kCreateGenesisNetwrokAccount = common::Encode::HexDecode(
    "b5be6f0090e4f5d40458258ed9adf843324c0327145c48b55091f33673d2d5a4");

struct BlockTxsItem {
    BlockTxsItem() : tx_ptr(nullptr), tx_count(0), success(false), leader_to_index(-1) {}
    pools::TxItemPtr tx_ptr;
    std::string tx_hash;
    uint32_t tx_count;
    uint64_t timeout;
    bool success;
    int32_t leader_to_index;
};

typedef std::shared_ptr<block::protobuf::Block> BlockPtr;

struct BlockToDbItem {
    BlockToDbItem(BlockPtr& bptr, const std::shared_ptr<db::DbWriteBatch>& batch)
        : block_ptr(bptr), db_batch(batch) {}
    BlockPtr block_ptr;
    std::shared_ptr<db::DbWriteBatch> db_batch;
};

struct LeaderWithToTxItem {
    std::shared_ptr<BlockTxsItem> to_txs[network::kConsensusShardEndNetworkId];
    uint64_t elect_height;
    uint32_t leader_idx;
    transport::MessagePtr to_txs_msg;
};


struct LeaderWithStatisticTxItem {
    LeaderWithStatisticTxItem() :
        shard_statistic_tx(nullptr),
        cross_statistic_tx(nullptr),
        elect_height(0),
        leader_idx(common::kInvalidUint32),
        statistic_msg(nullptr),
        leader_to_index(-1) {}
    std::shared_ptr<BlockTxsItem> shard_statistic_tx;
    std::shared_ptr<BlockTxsItem> cross_statistic_tx;
    uint64_t elect_height;
    uint32_t leader_idx;
    transport::MessagePtr statistic_msg;
    int32_t leader_to_index;
};

typedef std::shared_ptr<BlockToDbItem> BlockToDbItemPtr;

typedef std::function<bool(
    uint8_t thread_idx,
    const std::shared_ptr<block::protobuf::Block>& block,
    db::DbWriteBatch& db_batch)> DbBlockCallback;
typedef std::function<bool(
    const block::protobuf::Block& block)> BlockAggValidCallback;

}  // namespace block

}  // namespace zjchain
