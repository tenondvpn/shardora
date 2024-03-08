#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <atomic>
#include <functional>

#include "common/hash.h"
#include "common/encode.h"
#include "common/utils.h"
#include "common/log.h"
#include "protos/dht.pb.h"

#define DHT_DEBUG(fmt, ...) ZJC_DEBUG("[dht]" fmt, ## __VA_ARGS__)
#define DHT_INFO(fmt, ...) ZJC_INFO("[dht]" fmt, ## __VA_ARGS__)
#define DHT_WARN(fmt, ...) ZJC_WARN("[dht]" fmt, ## __VA_ARGS__)
#define DHT_ERROR(fmt, ...) ZJC_ERROR("[dht]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace dht {

class BaseDht;

enum DhtErrorCode {
    kDhtSuccess = 0,
    kDhtError = 1,
    kDhtInvalidNat = 2,
    kDhtNodeJoined = 3,
    kDhtInvalidBucket = 4,
    kDhtDesInvalid = 5,
    kDhtIpInvalid = 6,
    kDhtKeyInvalid = 7,
    kDhtClientMode = 8,
    kNodeInvalid = 9,
    kDhtKeyHashError = 10,
    kDhtGetBucketError = 11,
    kDhtMaxNeiborsError = 12,
    kDhtKeyInvalidCountry = 13,
};

enum BootstrapTag {
    kBootstrapNoInit = 0,
    kBootstrapInit = 1,
    kBootstrapInitWithConfNodes = 2,
};

enum NatType {
    kNatTypeUnknown = 0,
    kNatTypeFullcone = 1,
    kNatTypeAddressLimit = 2,
    kNatTypePortLimit = 3,
};

enum NodeJoinWay : int32_t {
    kJoinFromUnknown = 0,
    kJoinFromBootstrapRes,
    kJoinFromRefreshNeigberRequest,
    kJoinFromRefreshNeigberResponse,
    kJoinFromElectBlock,
    kJoinFromNetworkDetection,
    // if root and consensus shard must check valid
    kJoinFromBootstrapReq,
    kJoinFromConnect,
    kJoinFromDetection,
    kJoinFromUniversal,
};

static const uint32_t kDhtNearestNodesCount = 16u;
static const uint32_t kDhtMinReserveNodes = 4u;
static const uint32_t kDhtKeySize = 32u;
static const uint32_t kDhtMaxNeighbors = kDhtKeySize * 8 + kDhtNearestNodesCount;
static const uint32_t kRefreshNeighborsCount = 32u;
static const uint32_t kRefreshNeighborsDefaultCount = 32u;
static const uint32_t kRefreshNeighborsBloomfilterBitCount = 4096u;
static const uint32_t kRefreshNeighborsBloomfilterHashCount = 11u;
static const uint32_t kHeartbeatDefaultAliveTimes = 3u;

struct Node {
    uint64_t id_hash{ 0 };
    uint64_t dht_key_hash{ 0 };
    int32_t bucket{ 0 };
    int32_t heartbeat_times{ 0 };
    uint16_t public_port{ 0 };
    bool first_node{ false };
    std::atomic<uint32_t> heartbeat_send_times{ 0 };
    std::atomic<uint32_t> heartbeat_alive_times{ kHeartbeatDefaultAliveTimes };
    uint32_t join_way{ kJoinFromUnknown };
    int32_t sharding_id{ 0 };
    std::string id;
    std::string dht_key;
    std::string public_ip;
    std::string pubkey_str;

    Node();
    Node(
        int32_t in_sharding_id,
        const std::string& in_public_ip,
        uint16_t in_public_port,
        const std::string& in_pubkey_str,
        const std::string& in_id);
};

typedef std::shared_ptr<Node> NodePtr;
typedef std::function<void(
    BaseDht* dht,
    const protobuf::DhtMessage& dht_msg)> BootstrapResponseCallback;
typedef std::function<int(uint8_t, NodePtr& node)> NewNodeJoinCallback;
int DefaultDhtSignCallback(
    const std::string& peer_pubkey,
    const std::string& append_data,
    std::string* enc_data,
    std::string* sign_ch,
    std::string* sign_re);

}  // namespace dht

}  // namespace zjchain
