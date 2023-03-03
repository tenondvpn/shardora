#pragma once

#include <limits>

#include <libbls/tools/utils.h>

#include "common/utils.h"
#include "common/log.h"
#include "common/hash.h"
#include "common/global_info.h"
#include "common/thread_safe_queue.h"
#include "common/bitmap.h"
#include "consensus/waiting_txs.h"
#include "db/db.h"
#include "protos/block.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace consensus {

enum BftCheckTimeoutStatus {
    kTimeoutNormal = 0,
    kTimeout = 1,
    kTimeoutCallPrecommit = 2,
    kTimeoutCallReChallenge = 3,
    kTimeoutWaitingBackup = 4,
};

enum WaitingBlockType {
    kRootBlock,
    kSyncBlock,
    kToBlock,
};

struct BftItem {
    transport::MessagePtr msg_ptr;
    bool prepare_valid{ true };
};

struct ZbftItem {
    std::shared_ptr<WaitingTxsItem> txs_ptr;
    transport::MessagePtr msg_ptr;
    bool prepare_valid{ true };
};

typedef std::shared_ptr<BftItem> BftItemPtr;
struct LeaderPrepareItem {
    uint64_t height;
    std::vector<libff::alt_bn128_G1> backup_sign;
    std::unordered_set<std::string> precommit_aggree_set_;
    common::Bitmap prepare_bitmap_{ common::kEachShardMaxNodeCount };
    libff::alt_bn128_G1 backup_precommit_signs_[common::kEachShardMaxNodeCount];
    std::unordered_map<uint64_t, uint32_t> height_count_map;
};

struct PoolTxCountItem {
    PoolTxCountItem() {
        Clear();
    }

    uint64_t elect_height;
    int32_t pool_tx_counts[512];
    void Clear() {
        elect_height = 0;
        memset(pool_tx_counts, 0, sizeof(pool_tx_counts));
    }
};

struct PoolTxIndexItem {
    std::vector<uint32_t> pools;
    uint32_t prev_index;
};

static const uint32_t kBftOneConsensusMaxCount = 32u;  // every consensus
static const uint32_t kBftOneConsensusMinCount = 1u;
// bft will delay 500ms for all node ready

// broadcast default param
static const uint32_t kBftBroadcastIgnBloomfilterHop = 1u;
static const uint32_t kBftBroadcastStopTimes = 2u;
static const uint32_t kBftHopLimit = 5u;
static const uint32_t kBftHopToLayer = 2u;
static const uint32_t kBftNeighborCount = 7u;
static const uint32_t kBftTimeout = 14u * 1000u * 1000u;  // bft timeout 15s
// tx pool timeout 3 * kTimeBlockCreatePeriodSeconds seconds
static const uint32_t kTxPoolFinalStatisticTimeoutSeconds = /*kBftFinalStatisticStartDeltaTime / 1000000u + */30u;
static const uint32_t kTxPoolElectionTimeoutSeconds = /*kBftElectionStartDeltaTime / 1000000u + */30u;
static const uint32_t kBftTimeoutCheckPeriod = 10u * 1000u * 1000u;
static const uint32_t kBftLeaderPrepareWaitPeriod = 5u * 1000u * 1000u;
static const uint32_t kPrevTransportVersion = 0;
static const uint32_t kTransportVersion = 1;
static const int64_t kChangeLeaderTimePeriodSec = 30l;

std::string StatusToString(uint32_t status);
// hash128(gid + from + to + amount + type + attrs(k:v))
std::string GetTxMessageHash(const block::protobuf::BlockTx& tx_info);
// prehash + network_id + height + random + elect version + txes's hash
std::string GetPrepareTxsHash(const block::protobuf::BlockTx& tx_info);
std::string GetBlockHash(const block::protobuf::Block& block);
uint32_t NewAccountGetNetworkId(const std::string& addr);
bool IsRootSingleBlockTx(uint32_t tx_type);
bool IsShardSingleBlockTx(uint32_t tx_type);
bool IsShardSuperSingleBlockTx(uint32_t tx_type);

}  // namespace consensus

}  //namespace zjchain
