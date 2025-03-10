#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <protos/pools.pb.h>
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
#include "protos/view_block.pb.h"

#define BLOCK_DEBUG(fmt, ...) ZJC_DEBUG("[block]" fmt, ## __VA_ARGS__)
#define BLOCK_INFO(fmt, ...) ZJC_INFO("[block]" fmt, ## __VA_ARGS__)
#define BLOCK_WARN(fmt, ...) ZJC_WARN("[block]" fmt, ## __VA_ARGS__)
#define BLOCK_ERROR(fmt, ...) ZJC_ERROR("[block]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace block {

enum BlockErrorCode {
    kBlockSuccess = 0,
    kBlockError = 1,
    kBlockDbNotExists = 2,
    kBlockDbDataInvalid = 3,
    kBlockAddressNotExists = 4,
    kBlockVerifyAggSignFailed = 5,
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

static const uint64_t kStopConsensusTimeoutMs = 30000lu;

class BlockTxsItem {
public:
    BlockTxsItem() : tx_ptr(nullptr), tx_count(0), success(false), leader_to_index(-1) {
            stop_consensus_timeout = common::TimeUtils::TimestampMs() + kStopConsensusTimeoutMs;
        common::GlobalInfo::Instance()->AddSharedObj(0);
    }

    ~BlockTxsItem() {
        common::GlobalInfo::Instance()->DecSharedObj(0);
    }

    pools::TxItemPtr tx_ptr;
    std::string tx_hash;
    uint32_t tx_count;
    uint64_t timeout;
    bool success;
    int32_t leader_to_index;
    uint64_t stop_consensus_timeout;
};

typedef std::shared_ptr<block::protobuf::Block> BlockPtr;
typedef std::shared_ptr<view_block::protobuf::ViewBlockItem> ViewBlockPtr;

class BlockToDbItem {
public:
    BlockToDbItem(ViewBlockPtr& bptr, const std::shared_ptr<db::DbWriteBatch>& batch)
            : view_block_ptr(bptr), final_db_batch(batch) {
        common::GlobalInfo::Instance()->AddSharedObj(1);
    }
    
    ~BlockToDbItem() {
        common::GlobalInfo::Instance()->DecSharedObj(1);
    }

    ViewBlockPtr view_block_ptr;
    std::shared_ptr<db::DbWriteBatch> final_db_batch;
};

struct LeaderWithToTxItem {
    std::shared_ptr<BlockTxsItem> to_tx;
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

struct localToTxInfo {
    std::string des;
    uint64_t amount;
    uint32_t pool_index;
    // for ContractCreate
    std::string library_bytes;
    std::string contract_from;
    uint64_t contract_prepayment; // prepayment 交易的 prepayment 是通过 amount 传递的吧
    
    localToTxInfo(const std::string& des,
        uint64_t amount,
        uint32_t pool_index,
        const std::string& library_bytes,
        const std::string& contract_from,
        uint64_t prepayment) :
        des(des),
        amount(amount),
        pool_index(pool_index),
        library_bytes(library_bytes),
        contract_from(contract_from),
        contract_prepayment(prepayment) {}
};

inline bool isContractCreateToTxMessageItem(const pools::protobuf::ToTxMessageItem& tos_item) {
    return tos_item.has_library_bytes();
}

typedef std::shared_ptr<BlockToDbItem> BlockToDbItemPtr;

typedef std::function<bool(
    const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
    db::DbWriteBatch& db_batch)> DbBlockCallback;

}  // namespace block

}  // namespace shardora
