#pragma once

#include "common/utils.h"
#include "common/log.h"
#include "common/hash.h"
#include "common/global_info.h"

#define NETWORK_DEBUG(fmt, ...) DEBUG("[network]" fmt, ## __VA_ARGS__)
#define NETWORK_INFO(fmt, ...) ZJC_INFO("[network]" fmt, ## __VA_ARGS__)
#define NETWORK_WARN(fmt, ...) ZJC_WARN("[network]" fmt, ## __VA_ARGS__)
#define NETWORK_ERROR(fmt, ...) ZJC_ERROR("[network]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace network {

enum NetworkErrorCode {
    kNetworkSuccess = 0,
    kNetworkError = 1,
    kNetworkJoinUniversalError = 2,
    kNetworkJoinShardFailed = 3,
    kNetworkNoBootstrapNodes = 4,
    kNetworkNetworkJoined = 5,
    kNetworkNetworkNotJoined = 6,
};

// consensus shard 3 - 4097
// service shard 4098 - 10240
// universal 0
// node network 1
// root congress 2
static const uint32_t kUniversalNetworkId = 0u;  // all network join(for find network)
static const uint32_t kNodeNetworkId = 1u;  // just node id join(for broadcast)
static const uint32_t kRootCongressNetworkId = 2u;
static const uint32_t kConsensusShardBeginNetworkId = 3u;  // eq
static const uint32_t kConsensusShardEndNetworkId = 1026u;  // less
static const uint32_t kConsensusWaitingShardOffset = kConsensusShardEndNetworkId - kRootCongressNetworkId;
static const uint32_t kRootCongressWaitingNetworkId = kRootCongressNetworkId + kConsensusWaitingShardOffset;
static const uint32_t kConsensusWaitingShardBeginNetworkId = kRootCongressNetworkId + kConsensusWaitingShardOffset;  // eq
static const uint32_t kConsensusWaitingShardEndNetworkId = kConsensusShardEndNetworkId + kConsensusWaitingShardOffset;  // less

static const uint32_t kConsensusShardNetworkCount = (
        kConsensusShardEndNetworkId - kConsensusShardBeginNetworkId + 1);
static const uint32_t kServiceShardBeginNetworkId = kConsensusWaitingShardEndNetworkId;  // eq
static const uint32_t kServiceShardEndNetworkId = 2048;  // less

enum ServiceNetworkType {
    kVpnNetworkId = kServiceShardBeginNetworkId,
    kVpnRouteNetworkId,
    kConsensusSubscription,
    kVpnRouteVipLevel1NetworkId,
    kVpnRouteVipLevel2NetworkId,
    kVpnRouteVipLevel3NetworkId,
    kVpnRouteVipLevel4NetworkId,
    kVpnRouteVipLevel5NetworkId,
    kWaitingPoolNetworkId,
};

inline static bool IsSameShardOrSameWaitingPool(uint32_t local_net_id, uint32_t des_net_id) {
    if (des_net_id == local_net_id) {
        return true;
    }

    if (des_net_id >= kRootCongressNetworkId &&
            des_net_id < kConsensusShardEndNetworkId &&
            local_net_id == (des_net_id + kConsensusWaitingShardOffset)) {
        return true;
    }

    return false;
}

}  // namespace network

}  // namespace zjchain
