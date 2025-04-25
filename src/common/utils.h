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

#ifdef ZJC_TRACE_MESSAGE
struct Construct {
    uint32_t net_id;
    uint8_t country;
    uint8_t reserve1;
    uint8_t reserve2;
    uint8_t reserve3;
    char hash[24];
};

#define ZJC_NETWORK_DEBUG_FOR_PROTOMESSAGE(message, append) \
    do { \
        if ((message).has_debug()) { \
            Construct* src_cons_key = (Construct*)((message).src_dht_key().c_str()); \
            Construct* des_cons_key = (Construct*)((message).des_dht_key().c_str()); \
            ZJC_ERROR("[%s][handled: %d] [hash: %llu][hop: %d][src_net: %u][des_net: %u][id:%u]" \
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
#define ZJC_NETWORK_DEBUG_FOR_PROTOMESSAGE(message, append)
#endif

#ifndef NDEBUG
#define ADD_DEBUG_PROCESS_TIMESTAMP()
#define TMP_ADD_DEBUG_PROCESS_TIMESTAMP()

// #define ADD_DEBUG_PROCESS_TIMESTAMP() { \
//     if (msg_ptr) { \
//         assert(msg_ptr->times_idx < (sizeof(msg_ptr->times) / sizeof(msg_ptr->times[0]))); \
//         auto btime = common::TimeUtils::TimestampUs(); \
//         uint64_t diff_time = 0; \
//         if (msg_ptr->times_idx > 0) { diff_time = btime - msg_ptr->times[msg_ptr->times_idx - 1]; if (diff_time > 200000lu)ZJC_INFO("over handle message debug use time: %lu, type: %d", diff_time, msg_ptr->header.type());} \
//         msg_ptr->debug_str[msg_ptr->times_idx] = std::string(ZJC_LOG_FILE_NAME) + ":" + std::to_string(__LINE__); \
//         msg_ptr->times[msg_ptr->times_idx] = btime; \
//         msg_ptr->times_idx++; \
//     } \
// }

// #define TMP_ADD_DEBUG_PROCESS_TIMESTAMP() { \
//     if (msg_ptr) { \
//         assert(msg_ptr->times_idx < (sizeof(msg_ptr->times) / sizeof(msg_ptr->times[0]))); \
//         auto btime = common::TimeUtils::TimestampUs(); \
//         uint64_t diff_time = 0; \
//         if (msg_ptr->times_idx > 0) { diff_time = btime - msg_ptr->times[msg_ptr->times_idx - 1]; if (diff_time > 10000lu)ZJC_INFO("over handle message debug use time: %lu, type: %d", diff_time, msg_ptr->header.type());} \
//         msg_ptr->debug_str[msg_ptr->times_idx] = std::string(ZJC_LOG_FILE_NAME) + ":" + std::to_string(__LINE__); \
//         msg_ptr->times[msg_ptr->times_idx] = btime; \
//         msg_ptr->times_idx++; \
//     } \
// }
#else
#define ADD_DEBUG_PROCESS_TIMESTAMP()
#define TMP_ADD_DEBUG_PROCESS_TIMESTAMP()
#endif



#ifndef NDEBUG
#define CHECK_MEMORY_SIZE(data_map) { \
    if (data_map.size() >= 1024) { \
        ZJC_INFO("data size: %u", data_map.size()); \
    } \
}

#define CHECK_MEMORY_SIZE_WITH_MESSAGE(data_map, msg) { \
    if (data_map.size() >= 1024) { \
        ZJC_INFO("%s data size: %u, msg: %s", #data_map, data_map.size(), msg); \
    } \
}

#else
#define CHECK_MEMORY_SIZE(data_map)
#define CHECK_MEMORY_SIZE_WITH_MESSAGE(data_map, msg)
#endif

#ifndef NDEBUG
// #define ADD_TX_DEBUG_INFO(tx_proto)
#define ADD_TX_DEBUG_INFO(tx_proto) { \
    auto* tx_debug = tx_proto->add_tx_debug(); \
    tx_debug->set_tx_debug_tm_ms(common::TimeUtils::TimestampMs()); \
    tx_debug->set_tx_debug_info(std::string(ZJC_LOG_FILE_NAME) + ":" +  std::string(__FUNCTION__) + ":" + std::to_string(__LINE__)); \
}
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

static const uint32_t kUnicastAddressLength = 20u;
static const uint32_t kImmutablePoolSize = 32u;
static const uint32_t kMaxTxCount = 2048u;
static const uint32_t kInvalidPoolIndex = kImmutablePoolSize + 1;
static const uint32_t kTestForNetworkId = 4u;
static const uint16_t kDefaultVpnPort = 9033u;
static const uint16_t kDefaultRoutePort = 9034u;
// static const int64_t kRotationPeriod = 600ll * 1000ll * 1000ll; // epoch time
static const int64_t kRotationPeriod = 120ll * 1000ll * 1000ll; // for quicker debugging
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
static const uint64_t kTimeBlockCreatePeriodSeconds = kRotationPeriod / (1000ll * 1000ll);
static const uint64_t kLeaderRotationPeriodSeconds = 10llu;
static const uint32_t kEatchShardMaxSupperLeaderCount = 7u;
static const uint32_t kEachShardMinNodeCount = 3u;
static const uint32_t kEachShardMaxNodeCount = 1024u;
static const uint32_t kNodeModIndexMaxCount = 64u;
static const uint32_t kNodePublicIpMaskLen = 22u;  // node public ip just Mask length to protect security
static const int32_t kInitNodeCredit = 30;
static const double kMiningTokenMultiplicationFactor = 1.0;

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
static const uint8_t kMaxThreadCount = kImmutablePoolSize;

static const uint32_t kSingleBlockMaxMBytes = 2u;
static const uint32_t kVpnShareStakingPrice = 1u;

static const uint64_t kZjcMiniTransportUnit = 100000000llu;
static const uint64_t kZjcMaxAmount = 210llu * 100000000llu * kZjcMiniTransportUnit;
static const uint32_t kTransactionNoVersion = 0u;
static const uint32_t kTransactionVersion = 1u;
static const uint64_t kGenesisFoundationMaxZjc = kZjcMaxAmount / 100llu * 14llu;
static const uint64_t kGenesisShardingNodesMaxZjc = kZjcMaxAmount / 100llu * 1llu;
static const uint32_t kElectNodeMinMemberIndex = 1024u;

static const uint64_t kVpnVipMinPayfor = 66llu * kZjcMiniTransportUnit;
static const uint64_t kVpnVipMaxPayfor = 2000u * kZjcMiniTransportUnit;

static const uint32_t kDefaultBroadcastIgnBloomfilterHop = 1u;
static const uint32_t kDefaultBroadcastStopTimes = 2u;
static const uint32_t kDefaultBroadcastHopLimit = 5u;
static const uint32_t kDefaultBroadcastHopToLayer = 2u;
static const uint32_t kDefaultBroadcastNeighborCount = 7u;
static const uint64_t kBuildinTransactionGasPrice = 999999999lu;
static const std::string kRootPoolsAddressPrefix = common::Encode::HexDecode(
    "000000000000000000000000000000000000");

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

#ifndef NDEBUG
#define CheckThreadIdValid() { \
    static uint64_t count = 0; \
    ++count; \
    static std::thread::id init_thread_id = std::this_thread::get_id(); \
    auto now_thread_id = std::this_thread::get_id(); \
    ZJC_DEBUG("now handle thread id: %u, old: %u, count: %d", now_thread_id, init_thread_id, count); \
    if (count > 3) { \
        assert(init_thread_id == now_thread_id); \
    } else { \
        init_thread_id = now_thread_id; \
    } \
}
#else
#define CheckThreadIdValid()
#endif

}  // namespace common

}  // namespace shardora

