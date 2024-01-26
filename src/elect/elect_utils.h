#pragma once

#include <functional>

#include "common/log.h"
#include "common/limit_heap.h"
#include "common/node_members.h"
#include "common/hash.h"
#include "common/utils.h"
#include "protos/elect.pb.h"

#define ELECT_DEBUG(fmt, ...) ZJC_DEBUG("[elect]" fmt, ## __VA_ARGS__)
#define ELECT_INFO(fmt, ...) ZJC_INFO("[elect]" fmt, ## __VA_ARGS__)
#define ELECT_WARN(fmt, ...) ZJC_WARN("[elect]" fmt, ## __VA_ARGS__)
#define ELECT_ERROR(fmt, ...) ZJC_ERROR("[elect]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace elect {

enum ElectErrorCode {
    kElectSuccess = 0,
    kElectError = 1,
    kElectJoinUniversalError = 2,
    kElectJoinShardFailed = 3,
    kElectNoBootstrapNodes = 4,
    kElectNetworkJoined = 5,
    kElectNetworkNotJoined = 6,
};

struct HeapItem {
    uint32_t index;
    uint32_t succ_count;
};

typedef std::function<void(
    uint32_t sharding_id,
    uint64_t elect_height,
    common::MembersPtr& members,
    const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block)> NewElectBlockCallback;

inline bool operator<(const HeapItem& lhs, const HeapItem& rhs) {
    return lhs.succ_count < rhs.succ_count;
}

static const uint32_t kElectBroadcastIgnBloomfilterHop = 1u;
static const uint32_t kElectBroadcastStopTimes = 2u;
static const uint32_t kElectHopLimit = 5u;
static const uint32_t kElectHopToLayer = 2u;
static const uint32_t kElectNeighborCount = 7u;
static const uint32_t kInvalidMemberIndex = (std::numeric_limits<uint32_t>::max)();
static const uint32_t kMinShardingNetworkNodesCount = 7u;
// weed out and pick 1/10 nodes each epoch
static const uint32_t kFtsWeedoutDividRate = 10u;
static const uint32_t kInvalidShardNodesRate = 5u;
static const uint32_t kEachShardMaxTps = 2000u;
// Tolerate 5% difference between leader and backup
static const uint32_t kTolerateLeaderBackupFiffRate = 5u;  // kTolerateLeaderBackupFiffRate %;
static const uint64_t kSmoothGradientAmount = 100llu;

static const uint32_t kBloomfilterHashCount = 7u;
static const uint32_t kBloomfilterSize = 20480u;
static const uint32_t kBloomfilterWaitingSize = 40960u;
static const uint32_t kBloomfilterWaitingHashCount = 9u;

static const uint64_t kWaitingNodesGetTimeoffsetMilli = 30000llu;

// Nodes can participate in the election for more than 30 minutes after joining
// Set aside 5 minutes as the tolerance range, that is, each consensus node judges
// whether the node within the local tolerance range is in the master node sequence,
// if not, it is opposed
// static const uint64_t kElectAvailableJoinTime = 35llu * 60llu * 1000000llu;
// static const uint64_t kElectAvailableTolerateTime = 5llu * 60llu * 1000000llu;
static const uint64_t kElectAvailableJoinTime = 60llu * 1000000llu;
static const uint64_t kElectAvailableTolerateTime =5llu * 1000000llu;

inline static std::string GetElectHeartbeatHash(
        const std::string& ip,
        uint16_t port,
        uint32_t net_id,
        uint64_t tm) {
    std::string hash_str = ip + "_" +
        std::to_string(port) + "_" +
        std::to_string(net_id) + "_" +
        std::to_string(tm);
    return common::Hash::keccak256(hash_str);
}

}  // namespace elect

}  // namespace zjchain
