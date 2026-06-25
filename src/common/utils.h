#pragma once

#include <stdint.h>
#include <string.h>
#include <time.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "common/encode.h"
#include "common/log.h"
#include <algorithm>

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
        TypeName(const TypeName&); \
        TypeName& operator=(const TypeName&)
#endif  // !DISALLOW_COPY_AND_ASSIGN

#ifdef SHARDORA_TRACE_MESSAGE
struct Construct {
    uint32_t net_id;
    uint8_t country;
    uint8_t reserve1;
    uint8_t reserve2;
    uint8_t reserve3;
    char hash[24];
};

#define SHARDORA_NETWORK_DEBUG_FOR_PROTOMESSAGE(message, append) \
    do { \
        if ((message).has_debug()) { \
            Construct* src_cons_key = (Construct*)((message).src_dht_key().c_str()); \
            Construct* des_cons_key = (Construct*)((message).des_dht_key().c_str()); \
            SHARDORA_ERROR("[%s][handled: %d] [hash: %llu][hop: %d][src_net: %u][des_net: %u][id:%u]" \
                "[broad: %d][universal: %d][type: %d] %s", \
                (message).debug().c_str(), \
                (message).handled(), \
                (message).hash(), \
                (message).hop_count(), \
                src_cons_key->net_id, \
                des_cons_key->net_id, \
                (message).id(), \
                (message).has_broadcast(), \
                (message).universal(), \
                (message).type(), \
                (std::string(append)).c_str()); \
        } \
    } while (0)
#else
#define SHARDORA_NETWORK_DEBUG_FOR_PROTOMESSAGE(message, append)
#endif

#ifndef NDEBUG
// #define ADD_DEBUG_PROCESS_TIMESTAMP()
#define TMP_ADD_DEBUG_PROCESS_TIMESTAMP()

#define ADD_DEBUG_PROCESS_TIMESTAMP() { \
    if (msg_ptr) { \
        auto btime = common::TimeUtils::TimestampUs(); \
        uint64_t diff_time = 0; \
        if (msg_ptr->times_idx > 0) { diff_time = btime - msg_ptr->times[msg_ptr->times_idx - 1]; if (diff_time > 200000lu)SHARDORA_DEBUG("over handle message debug use time: %lu, type: %d", diff_time, msg_ptr->header.type());} \
        msg_ptr->debug_str[msg_ptr->times_idx] = std::string(SHARDORA_LOG_FILE_NAME) + ":" + std::to_string(__LINE__); \
        msg_ptr->times[msg_ptr->times_idx] = btime; \
        msg_ptr->times_idx++; \
    } \
}

#define CheckThreadIdValid()
// #define CheckThreadIdValid() { \
//     if (common::GlobalInfo::Instance()->main_inited_success()) { \
//         ++local_thread_id_count_; \
//         auto now_thread_id = std::this_thread::get_id(); \
//         uint32_t now_id_val = (uint32_t)std::hash<std::thread::id>{}(now_thread_id); \
//         uint32_t init_id_val = (uint32_t)std::hash<std::thread::id>{}(local_thread_id_); \
//         SHARDORA_DEBUG("now handle thread id: %u, old: %u, count: %d", now_id_val, init_id_val, (int32_t)local_thread_id_count_); \
//         if (local_thread_id_count_ > 3) { \
//             //assert(local_thread_id_ == now_thread_id); \
//         } else { \
//             local_thread_id_ = now_thread_id; \
//         } \
//     } \
// }

// #define TMP_ADD_DEBUG_PROCESS_TIMESTAMP() { \
//     if (msg_ptr) { \
//         //assert(msg_ptr->times_idx < (sizeof(msg_ptr->times) / sizeof(msg_ptr->times[0]))); \
//         auto btime = common::TimeUtils::TimestampUs(); \
//         uint64_t diff_time = 0; \
//         if (msg_ptr->times_idx > 0) { diff_time = btime - msg_ptr->times[msg_ptr->times_idx - 1]; if (diff_time > 10000lu)SHARDORA_DEBUG("over handle message debug use time: %lu, type: %d", diff_time, msg_ptr->header.type());} \
//         msg_ptr->debug_str[msg_ptr->times_idx] = std::string(SHARDORA_LOG_FILE_NAME) + ":" + std::to_string(__LINE__); \
//         msg_ptr->times[msg_ptr->times_idx] = btime; \
//         msg_ptr->times_idx++; \
//     } \
// }
#else
#define ADD_DEBUG_PROCESS_TIMESTAMP() { \
    if (msg_ptr) { \
        auto btime = common::TimeUtils::TimestampUs(); \
        uint64_t diff_time = 0; \
        if (msg_ptr->times_idx > 0) { diff_time = btime - msg_ptr->times[msg_ptr->times_idx - 1]; if (diff_time > 200000lu)SHARDORA_DEBUG("over handle message debug use time: %lu, type: %d", diff_time, msg_ptr->header.type());} \
        msg_ptr->debug_str[msg_ptr->times_idx] = std::string(SHARDORA_LOG_FILE_NAME) + ":" + std::to_string(__LINE__); \
        msg_ptr->times[msg_ptr->times_idx] = btime; \
        msg_ptr->times_idx++; \
    } \
}
// #define ADD_DEBUG_PROCESS_TIMESTAMP()
#define TMP_ADD_DEBUG_PROCESS_TIMESTAMP()
#define CheckThreadIdValid()
#endif

#ifndef NDEBUG
#define ADD_TX_DEBUG_INFO(tx_proto)
// #define ADD_TX_DEBUG_INFO(tx_proto) { \
//     auto* tx_debug = tx_proto->add_tx_debug(); \
//     tx_debug->set_tx_debug_tm_ms(common::TimeUtils::TimestampMs()); \
//     tx_debug->set_tx_debug_info(std::string(SHARDORA_LOG_FILE_NAME) + ":" +  std::string(__FUNCTION__) + ":" + std::to_string(__LINE__)); \
// }
#else
#define ADD_TX_DEBUG_INFO(tx_proto)
#endif

namespace shardora {

namespace common {

enum MessageType {
    kDhtMessage = 0,
    kNetworkMessage = 1,
    kSyncMessage = 2,
    kConsensusMessage = 3,
    kElectMessage = 4,
    kVssMessage = 5,
    kBlsMessage = 6,
    kPoolsMessage = 7,
    kContractMessage = 8,
    kBlockMessage = 9,
    kConsensusTimerMessage = 10,
    kPoolTimerMessage = 11,
    kInitMessage = 12,
    kC2cMessage = 13,
    kHotstuffSyncMessage = 14,
    kHotstuffTimeoutMessage = 15,
    kHotstuffMessage = 16,
    kHotstuffSyncTimerMessage = 17,
    kPacemakerTimerMessage = 18,
    // max (message) type
    kMaxMessageTypeCount,
};

enum CommonErrorCode {
    kCommonSuccess = 0,
    kCommonError = 1,
};

enum GetHeightBlockType {
    kHeightBlockTransactions = 0,
    kHeightBlockVersion = 1,
};

enum JoinRootFlag {
    kRandom = 0,
    kJoinRoot = 1,
    kNotJoinRoot = 2,
};

enum ConsensusType {
    kConsensusInvalidType = 0,
    kConsensusCreateGenesisConsensusNetwork = 1,
    kConsensusCreateGenesisAcount = 2,
    kConsensusCreateAcount = 3,
    kConsensusCreateContract = 4,
    kConsensusTransaction = 5,
    kConsensusCallContract = 6,
    kConsensusRootElectShard = 7,  // shard consensus network election
    kConsensusRootTimeBlock = 8,  // create time block
};

enum ClientStatus {
    kValid = 0,
    kBandwidthFreeToUseExceeded = 1,
    kPayForExpired = 2,
    kServerOverLoaded = 3,
    kLoginByOtherTerminal = 4,
};


enum ClientPlatform {
    kUnknown = 0,
    kIos = 1,
    kAndroid = 2,
    kMac = 3,
    kWindows = 4,
};

enum VipLevel {
    kNotVip = 0,
    kVipLevel1 = 1,
    kVipLevel2 = 2,
    kVipLevel3 = 3,
    kVipLevel4 = 4,
    kVipLevel5 = 5,
};

/**
 * Validation result codes
 */
enum class ValidationStatus {
    SUCCESS = 0,
    EMPTY_BYTECODE = 1,
    INCOMPLETE_PUSH = 2      // PUSH instruction is missing data bytes
};

static const uint32_t kUnicastAddressLength = 20u;
static const uint32_t kPreypamentAddressLength = 40u;
static const uint32_t kImmutablePoolSize = 32u;
static const uint32_t kGlobalPoolIndex = kImmutablePoolSize;
static const uint32_t kMaxTxCount = 20480u;
static const uint32_t kInvalidPoolIndex = kImmutablePoolSize + 1;
static const uint32_t kTestForNetworkId = 4u;
static const uint16_t kDefaultVpnPort = 9033u;
static const uint16_t kDefaultRoutePort = 9034u;
// static const int64_t kRotationPeriod = 600ll * 1000ll * 1000ll; // epoch time
static const int64_t kRotationPeriod = 60000000ll * 1000ll * 1000ll; // for quicker debugging
static const int64_t kMessageTimeoutMs = 10000ll;
static const uint32_t kMaxRotationCount = 4u;
static const uint16_t kNodePortRangeMin = 1000u;
static const uint16_t kNodePortRangeMax = 10000u;
static const uint16_t kVpnServerPortRangeMin = 10000u;
static const uint16_t kVpnServerPortRangeMax = 35000u;
static const uint16_t kVpnRoutePortRangeMin = 35000u;
static const uint16_t kVpnRoutePortRangeMax = 65000u;
static const uint16_t kRouteUdpPortRangeMin = 65000u;
static const uint16_t kRouteUdpPortRangeMax = 65100u;
static const uint16_t kVpnUdpPortRangeMin = 65100u;
static const uint16_t kVpnUdpPortRangeMax = 65200u;
static const uint64_t kTimeBlockCreatePeriodSeconds = (kRotationPeriod / (1000llu * 1000llu) - 30llu);
static const uint64_t kLeaderRotationPeriodSeconds = 10llu;
static const uint32_t kEatchShardMaxSupperLeaderCount = 7u;
static const uint32_t kEachShardMinNodeCount = 3u;
static const uint32_t kEachShardMaxNodeCount = 1024u;
static const uint32_t kNodeModIndexMaxCount = 64u;
static const uint32_t kNodePublicIpMaskLen = 22u;  // node public ip just Mask length to protect security
static const int32_t kInitNodeCredit = 30;
static const double kMiningTokenMultiplicationFactor = 1.0;
static const int32_t kLeaderRoatationBaseTimeoutSec = 30;

// Economic Model Parameters (Dynamic Sharding Reward System)
// kShardoraMiniTransportUnit must be defined before use (= 10^8, smallest SHARDORA unit)
static const uint64_t kShardoraMiniTransportUnit = 100000000llu;
static const uint64_t kInitialTotalReward = 10000llu * kShardoraMiniTransportUnit;  // 10,000 SHARDORA total per epoch
// Halving period: 4 years with 600s epoch period
// 4 years = 365.25 * 24 * 3600 / 600 * 4 = 210,240 epochs
static const uint32_t kHalvingPeriodEpochs = 210240u;  // 4 years (with 600s epoch period)
static const double kTxBonusMultiplier = 0.2;  // Transaction bonus up to 20% of shard reward
static const double kStakingRewardRatio = 0.0;  // Staking rewards (reserved for future)
static const double kBurnRatio = 0.5;  // Burn 50% of gas fees
static const uint64_t kMinBlockReward = 1llu * kShardoraMiniTransportUnit;  // Minimum reward 1 SHARDORA
static const uint32_t kMaxHalvingCount = 64u;  // Maximum halving iterations (prevent overflow)

// Dynamic Sharding Parameters
static const double kEarlyBonusMultiplier = 1.1;  // 10% bonus when shards < max
static const double kGenerationWeightDecay = 0.9;  // Each generation gets 90% of previous
static const uint32_t kMaxShardCount = 1024u;  // Maximum shard count
static const uint32_t kInitialShardCount = 3u;  // Initial shard count (Gen 0)

// Shard Generation Information
struct ShardGenerationInfo {
    uint32_t generation;        // Generation number
    uint32_t start_shard_id;    // Start shard ID (inclusive)
    uint32_t end_shard_id;      // End shard ID (inclusive)
    double weight;              // Weight coefficient (0.9^generation)
    uint32_t shard_count;       // Number of shards in this generation
};

// Shard generation table: 3 -> 8 -> 16 -> 32 -> 64 -> 128 -> 256 -> 512 -> 1024
static const ShardGenerationInfo kShardGenerations[] = {
    {0, 3, 5, 1.0, 3},                    // Gen 0: 3 shards (IDs: 3, 4, 5)
    {1, 6, 10, 0.9, 5},                   // Gen 1: 5 shards (IDs: 6-10)
    {2, 11, 18, 0.81, 8},                 // Gen 2: 8 shards (IDs: 11-18)
    {3, 19, 34, 0.729, 16},               // Gen 3: 16 shards (IDs: 19-34)
    {4, 35, 66, 0.6561, 32},              // Gen 4: 32 shards (IDs: 35-66)
    {5, 67, 130, 0.59049, 64},            // Gen 5: 64 shards (IDs: 67-130)
    {6, 131, 258, 0.531441, 128},         // Gen 6: 128 shards (IDs: 131-258)
    {7, 259, 514, 0.478297, 256},         // Gen 7: 256 shards (IDs: 259-514)
    {8, 515, 1026, 0.430467, 512}         // Gen 8: 512 shards (IDs: 515-1026)
};

static const uint32_t kShardGenerationCount = sizeof(kShardGenerations) / sizeof(ShardGenerationInfo);

static const uint64_t kToPeriodMs = 10000lu;

// broadcast default params
static const uint32_t kBroadcastDefaultNeighborCount = 7u;
static const uint32_t kBloomfilterBitSize = 256u;
static const uint32_t kBloomfilterHashCount = 3u;
static const uint32_t kBroadcastHopLimit = 10u;
static const uint32_t kBroadcastHopToLayer = 1u;
static const uint32_t kBroadcastIgnBloomfilter = 1u;

static const int64_t kInvalidInt64 = (std::numeric_limits<int64_t>::max)();
static const uint64_t kInvalidUint64 = (std::numeric_limits<uint64_t>::max)();
static const uint32_t kInvalidUint32 = (std::numeric_limits<uint32_t>::max)();
static const uint32_t kInvalidUint8 = (std::numeric_limits<uint8_t>::max)();
static const uint32_t kInvalidInt32 = (std::numeric_limits<int32_t>::max)();
static const uint32_t kInvalidFloat = (std::numeric_limits<float>::max)();
static const uint8_t kMaxThreadCount = 32u;

static const uint32_t kSingleBlockMaxMBytes = 2u;
// Maximum propose message size in bytes. Used by block_wrapper (tx packing limit)
// and hotstuff (oversized message detection). Leave headroom below the 2.5MB
// transport assert for headers, signatures, and protobuf overhead.
static const int kMaxProposeMsgBytes = 1 * 1024 * 1024;  // 1M
static const uint32_t kVpnShareStakingPrice = 1u;

static const uint64_t kShardoraMaxAmount = 1000000000000000llu * 100000000llu * kShardoraMiniTransportUnit;
static const uint32_t kTransactionNoVersion = 0u;
static const uint32_t kTransactionVersion = 1u;
// 10%
static const uint64_t kGenesisShardingNodesMaxShardora = kShardoraMaxAmount / 100llu * 10llu;
static const uint32_t kElectNodeMinMemberIndex = 1024u;

static const uint64_t kVpnVipMinPayfor = 66llu * kShardoraMiniTransportUnit;
static const uint64_t kVpnVipMaxPayfor = 2000u * kShardoraMiniTransportUnit;

static const uint32_t kDefaultBroadcastIgnBloomfilterHop = 1u;
static const uint32_t kDefaultBroadcastStopTimes = 2u;
static const uint32_t kDefaultBroadcastHopLimit = 5u;
static const uint32_t kDefaultBroadcastHopToLayer = 2u;
static const uint32_t kDefaultBroadcastNeighborCount = 7u;
static const uint64_t kBuildinTransactionGasPrice = 999999999lu;
static const std::string kRootPoolsAddressPrefix = common::Encode::HexDecode(
    "00000000000000000000000000000000");

#pragma pack(push)
#pragma pack(1)
union DhtKey {
    DhtKey() {
        memset(dht_key, 0, sizeof(dht_key));
    }

    struct Construct {
        uint32_t net_id;
        char hash[28];
    } construct;
    char dht_key[32];
};
#pragma pack(pop)


std::string CreateGID(const std::string& pubkey);
std::string FixedCreateGID(const std::string& str);

inline static std::string GetTxDbKey(bool from, const std::string& gid) {
    if (from) {
        return std::string("TX_from_") + gid;
    } else {
        return std::string("TX_to_") + gid;
    }
}

inline static std::string TimestampToDatetime(time_t timestamp) {
    struct tm* p = localtime(&timestamp);
    char time_str[64];
    memset(time_str, 0, sizeof(time_str));
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", p);
    return time_str;
}

inline static std::string MicTimestampToLiteDatetime(int64_t timestamp) {
#ifndef _WIN32
    int64_t milli = timestamp + (int64_t)(8 * 60 * 60 * 1000);
    auto mTime = std::chrono::milliseconds(milli);
    auto tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(mTime);
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm* now = std::gmtime(&tt);
    char time_str[64];
    snprintf(time_str, sizeof(time_str), "%02d/%02d %02d:%02d",
            now->tm_mon + 1,
            now->tm_mday,
            now->tm_hour,
            now->tm_min);
    return time_str;
#else
    int64_t milli = timestamp + (int64_t)(8 * 60 * 60 * 1000);
    auto mTime = std::chrono::milliseconds(milli);
    auto tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(mTime);
    __time64_t tt = std::chrono::system_clock::to_time_t(tp);
    struct tm  now;
    _localtime64_s(&now, &tt);
    char time_str[64];
    snprintf(time_str, sizeof(time_str), "%02d/%02d %02d:%02d",
            now.tm_mon + 1,
            now.tm_mday,
            now.tm_hour,
            now.tm_min);
    return time_str;
#endif
}

inline static std::string MicTimestampToDatetime(int64_t timestamp) {
#ifndef _WIN32
    int64_t milli = timestamp + (int64_t)(8 * 60 * 60 * 1000);
    auto mTime = std::chrono::milliseconds(milli);
    auto tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(mTime);
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm* now = std::gmtime(&tt);
    char time_str[64];
    snprintf(time_str, sizeof(time_str), "%4d/%02d/%02d %02d:%02d:%02d",
            now->tm_year + 1900,
            now->tm_mon + 1,
            now->tm_mday,
            now->tm_hour,
            now->tm_min,
            now->tm_sec);
    return time_str;
#else
    int64_t milli = timestamp + (int64_t)(8 * 60 * 60 * 1000);
    auto mTime = std::chrono::milliseconds(milli);
    auto tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(mTime);
    __time64_t tt = std::chrono::system_clock::to_time_t(tp);
    struct tm  now;
    _localtime64_s(&now, &tt);
    char time_str[64];
    snprintf(time_str, sizeof(time_str), "%4d/%02d/%02d %02d:%02d:%02d",
            now.tm_year + 1900,
            now.tm_mon + 1,
            now.tm_mday,
            now.tm_hour,
            now.tm_min,
            now.tm_sec);
    return time_str;
#endif
}

inline static uint32_t ShiftUint32(uint32_t value) {
    return (value & 0x000000FFU) << 24 | (value & 0x0000FF00U) << 8 |
        (value & 0x00FF0000U) >> 8 | (value & 0xFF000000U) >> 24;
}


inline static uint64_t ShiftUint64(uint64_t value) {
    uint64_t high_uint64 = uint64_t(ShiftUint32(uint32_t(value)));
    uint64_t low_uint64 = (uint64_t)ShiftUint32(uint32_t(value >> 32));
    return (high_uint64 << 32) + low_uint64;
}

uint32_t MicTimestampToDate(int64_t timestamp);

uint8_t RandomCountry();

void itimeofday(long *sec, long *usec);
int64_t iclock64(void);
uint32_t iclock();
void SignalRegister();
int RemoteReachable(const std::string& ip, uint16_t port, bool* reachable);
bool IsVlanIp(const std::string& ip);
uint32_t IpToUint32(const char* ip);
std::string Uint32ToIp(uint32_t ip);
uint32_t GetAddressPoolIndex(const std::string& addr);
std::string GetPoolAddress(uint32_t pool_index);
std::string GetRootStakePoolAddress();
uint32_t GetAddressMemberIndex(const std::string& addr);

inline static uint64_t GetNodeConnectInt(const std::string& ip, uint16_t port) {
    uint32_t int_ip = IpToUint32(ip.c_str());
    uint64_t res = ((uint64_t)int_ip) << 32 | ((uint64_t)port & 0x000000000000FFFFllu);
    return res;
}

inline static uint32_t GetSignerCount(uint32_t n) {
    auto t = n * 2 / 3;
    if ((n * 2) % 3 > 0) {
        t += 1;
    }

    return t;
}

template <class KeyType> uint32_t Hash32(const KeyType &t) { return 0; }


inline uint64_t GetNthElement(std::vector<uint64_t> v, float ratio) {
    size_t n = v.size() * ratio - 1;
    std::nth_element(v.begin(), v.begin() + n, v.end());
    return v[n];
}

template <typename Func, typename... Args>
bool Retry(Func func, int maxAttempts, std::chrono::milliseconds delay, Args... args) {
    for(int i = 0; i < maxAttempts; ++i) {
        if(func(std::forward<Args>(args)...)) {
            return true;
        }
        std::this_thread::sleep_for(delay);
    }
    return false;
}

static inline bool isFileExist(const std::string& path) {
    return std::filesystem::exists(path);
}

ValidationStatus IsContractBytescodeValid(const std::string& hex);

}  // namespace common

}  // namespace shardora

