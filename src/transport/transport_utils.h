#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <WinSock2.h>
#else
#include <unistd.h>
#endif

#include <memory>
#include <functional>

#include "common/log.h"
#include "common/time_utils.h"
#include "tnet/tcp_interface.h"
#include "protos/address.pb.h"
#include "protos/transport.pb.h"

#define TRANSPORT_DEBUG(fmt, ...) ZJC_DEBUG("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_INFO(fmt, ...) ZJC_INFO("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_WARN(fmt, ...) ZJC_WARN("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_ERROR(fmt, ...) ZJC_ERROR("[transport]" fmt, ## __VA_ARGS__)

namespace zjchain {

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

static const uint64_t kConsensusMessageTimeoutUs = 5000000lu;
static const uint64_t kHandledTimeoutMs = 5000lu;
static const uint64_t kMessagePeriodUs = 1500000lu;

struct TransportMessage {
    TransportMessage() : conn(nullptr), response(nullptr), tmp_ptr(nullptr), retry(false) {
        timeout = common::TimeUtils::TimestampUs() + kConsensusMessageTimeoutUs;
        handle_timeout = common::kInvalidUint64;
        prev_timestamp = common::TimeUtils::TimestampUs() + kMessagePeriodUs;
        memset(times, 0, sizeof(times));
        times_idx = 0;
    }

    protobuf::Header header;
    tnet::TcpInterface* conn = nullptr;
    uint8_t thread_idx = -1;
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    std::string msg_hash;
    std::shared_ptr<TransportMessage> response;
    void* tmp_ptr;
    bool retry;
    uint64_t times[128];
    uint32_t times_idx;
    uint64_t handle_timeout;
    uint64_t timeout;
    uint64_t prev_timestamp;
};

typedef std::shared_ptr<TransportMessage> MessagePtr;
typedef std::function<void(const transport::MessagePtr& message)> MessageProcessor;

static const uint32_t kMaxHops = 20u;
static const uint32_t kMaxMessageReserveCount = 10240;
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

}  // namespace zjchain
