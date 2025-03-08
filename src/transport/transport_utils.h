#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <WinSock2.h>
#else
#include <unistd.h>
#endif

#include <atomic>
#include <memory>
#include <functional>

#include "common/log.h"
#include "common/string_utils.h"
#include "common/time_utils.h"
#include "protos/address.pb.h"
#include "protos/transport.pb.h"
#include "tnet/tcp_interface.h"
#include "tnet/tcp_connection.h"

#define TRANSPORT_DEBUG(fmt, ...) ZJC_DEBUG("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_INFO(fmt, ...) ZJC_INFO("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_WARN(fmt, ...) ZJC_WARN("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_ERROR(fmt, ...) ZJC_ERROR("[transport]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace transport {

enum TransportErrorCode {
    kTransportSuccess = 0,
    kTransportError = 1,
    kTransportTimeout = 2,
    kTransportClientSended = 3,
};

enum TransportPriority {
    kTransportPrioritySystem = 0,
    kTransportPriorityHighest = 1,
    kTransportPriorityHigh = 2,
    kTransportPriorityMiddle = 3,
    kTransportPriorityLow = 4,
    kTransportPriorityLowest = 5,
    kTransportPriorityMaxCount,
};

struct TransportHeader {
    uint16_t size;
    uint16_t type;
    uint32_t server_id;
    uint32_t msg_no;
    uint16_t context_id;
    uint16_t frag_len;
    uint32_t msg_index;
    uint32_t epoch;
    uint16_t fec_no;
    uint16_t fec_index;
    struct {
        uint8_t frag_no;
        uint8_t frag_sum;
        uint16_t mtu;
    } frag;
};

enum TcpConnnectionType {
    kAddServerConnection = 0,
    kRemoveServerConnection = 1,
    kAddClient = 2,
    kRemoveClient = 3,
};

enum FirewallCheckStatus {
    kFirewallCheckSuccess = 0,
    kFirewallCheckError = 1,
};

static const uint64_t kConsensusMessageTimeoutUs = 5000000lu;
static const uint64_t kHandledTimeoutMs = 10000lu;
static const uint64_t kMessagePeriodUs = 1500000lu;

// TODO: check memory
class TransportMessage {
public:
    static std::atomic<int32_t> testTransportMessageCount;
    TransportMessage() : conn(nullptr), retry(false), handled(false), is_leader(false) {
        timeout = common::TimeUtils::TimestampUs() + kConsensusMessageTimeoutUs;
        handle_timeout = common::kInvalidUint64;
        prev_timestamp = common::TimeUtils::TimestampUs() + kMessagePeriodUs;
        memset(times, 0, sizeof(times));
        times_idx = 0;
        thread_index = -1;
        ZJC_DEBUG("memory check create new transport message: %d", testTransportMessageCount.fetch_add(1));
    }

    ~TransportMessage() {
        ZJC_DEBUG("memory check remove transport message: %d", testTransportMessageCount.fetch_sub(1));
    }

    protobuf::Header header;
    std::shared_ptr<tnet::TcpInterface> conn = nullptr;
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    std::string msg_hash;
    bool retry;
    uint64_t times[256];
    std::string debug_str[256];
    uint32_t times_idx;
    uint64_t handle_timeout;
    uint64_t timeout;
    uint64_t prev_timestamp;
    bool handled;
    bool is_leader;
    int32_t thread_index;
};

typedef std::shared_ptr<TransportMessage> MessagePtr;
typedef std::function<void(const transport::MessagePtr& message)> MessageProcessor;
typedef std::function<int(transport::MessagePtr& message)> FirewallCheckCallback;

struct ClientItem {
    std::string des_ip;
    uint16_t port;
    std::string msg;
    uint64_t hash64;
};

struct ClientConnection {
    std::string des_ip;
    uint16_t port;
    tnet::TcpConnection* conn;
};

static const uint32_t kMaxHops = 20u;
static const uint32_t kMaxMessageReserveCount = 102400;
static const uint32_t kBroadcastMaxRelayTimes = 2u;
static const uint32_t kBroadcastMaxMessageCount = 1024u * 1024u;
static const uint32_t kUniqueMaxMessageCount = 10u * 1024u;
static const uint32_t kKcpRecvWindowSize = 128u;
static const uint32_t kKcpSendWindowSize = 128u;
static const uint32_t kMsgPacketMagicNum = 345234223;
static const int32_t kTransportTxBignumVersionNum = 1;
static const int32_t kTransportVersionNum = 2;
static const int32_t kTcpBuffLength = 2 * 1024 * 1024;

inline void CloseSocket(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

}  // namespace transport

}  // namespace shardora
