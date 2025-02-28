#pragma once

#include <limits>

#include <libbls/tools/utils.h>

#include "common/bitmap.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/log.h"
#include "common/node_members.h"
#include "common/thread_safe_queue.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "db/db.h"
#include "protos/block.pb.h"
#include "transport/transport_utils.h"

namespace shardora {

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
static const uint32_t kSyncFromOtherCount = 1u;
static const uint64_t kElectBlockValidTimeMs = 10000lu;
static const uint64_t kRemovePrecommitedBftTimeUs = common::kRotationPeriod;

class WaitingTxsItem;
struct ZbftItem {
    std::shared_ptr<WaitingTxsItem> txs_ptr;
    transport::MessagePtr msg_ptr;
    bool prepare_valid{ true };
};

struct LeaderPrepareItem {
    uint64_t height;
    std::unordered_set<std::string> precommit_aggree_set_;
    common::Bitmap prepare_bitmap_{ common::kEachShardMaxNodeCount };
    libff::alt_bn128_G1 backup_precommit_signs_[common::kEachShardMaxNodeCount];
    std::vector<uint32_t> valid_index;
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
    PoolTxIndexItem() : valid_ip_count(0), synced_ip(false) {
        memset(member_ips, 0, sizeof(member_ips));
    }

    std::vector<uint32_t> pools;
    uint32_t prev_index;
    uint32_t member_ips[common::kEachShardMaxNodeCount];
    std::unordered_map<uint32_t, int32_t> all_members_ips[common::kEachShardMaxNodeCount];
    int32_t valid_ip_count;
    bool synced_ip;
};

struct ElectItem {
    ElectItem() : members(nullptr), local_member(nullptr),
            elect_height(0), local_node_member_index(common::kInvalidUint32), bls_valid(false) {
        time_valid = common::TimeUtils::TimestampMs() + kElectBlockValidTimeMs;
        change_leader_time_valid = time_valid + kElectBlockValidTimeMs;
        invalid_time = time_valid + common::kRotationPeriod / 1000lu;
        for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
            thread_set[i] = nullptr;
        }

        auto count = sizeof(mod_with_leader_index) / sizeof(mod_with_leader_index[0]);
        for (int32_t i = 0; i < count; ++i) {
            mod_with_leader_index[i] = -1;
        }
    }

    common::MembersPtr members;
    common::BftMemberPtr local_member;
    int32_t leader_count;
    int32_t member_size;
    uint64_t elect_height;
    uint32_t local_node_member_index;
    std::shared_ptr<PoolTxIndexItem> thread_set[common::kMaxThreadCount];
    libff::alt_bn128_G2 common_pk;
    libff::alt_bn128_Fr sec_key;
    bool bls_valid;
    uint64_t time_valid;
    uint64_t change_leader_time_valid;
    uint64_t invalid_time;
    volatile int32_t mod_with_leader_index[256];
};

struct BftMessageInfo {
    BftMessageInfo(const std::string& tmp_gid) : gid(tmp_gid) {
        for (int32_t i = 0; i < 3; ++i) {
            msgs[i] = nullptr;
        }
    }

    transport::MessagePtr msgs[3];
    std::string gid;
};

std::string StatusToString(uint32_t status);

std::string GetCommitedBlockHash(const std::string& prepare_hash);
uint32_t NewAccountGetNetworkId(const std::string& addr);
bool IsRootSingleBlockTx(uint32_t tx_type);
bool IsShardSingleBlockTx(uint32_t tx_type);
bool IsShardSuperSingleBlockTx(uint32_t tx_type);

}  // namespace consensus

}  //namespace shardora
