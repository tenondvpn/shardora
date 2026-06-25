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
#include "common/global_info.h"
#include "common/string_utils.h"
#include "common/time_utils.h"
#include "protos/address.pb.h"
#include "protos/transport.pb.h"
#include "tnet/tcp_interface.h"
#include "tnet/tcp_connection.h"

#define TRANSPORT_DEBUG(fmt, ...) SHARDORA_DEBUG("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_INFO(fmt, ...) SHARDORA_DEBUG("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_WARN(fmt, ...) SHARDORA_WARN("[transport]" fmt, ## __VA_ARGS__)
#define TRANSPORT_ERROR(fmt, ...) SHARDORA_ERROR("[transport]" fmt, ## __VA_ARGS__)

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

enum MessageHandleStatus : int32_t {
    kConsensusSuccess = 0,
    kMessageHandle = 10001,
    kMessageHandleError = 10002,
    kTxAccept = 10003,
    kTxInvalidSignature = 10004,
    kTxInvalidAddress = 10005,
    kTxPoolFullReject = 10006,
    kTxUserNonceInvalid = 10007,
    kUnkonwn = 10008,
    kRequestInvalid = 10009,
    kNotExists = 100010,

    kEvmcSuccess = 0,
    kEvmcFailure = 1,
    kEvmcRevert = 2,
    kEvmcOutOfGas = 3,
    kEvmcInvalidInstruction = 4,
    kEvmcUndefinedInstruction = 5,
    kEvmcStackOverflow = 6,
    kEvmcStackUnderflow = 7,
    kEvmcBadJumpDestination = 8,
    kEvmcInvalidMemoryAccess = 9,
    kEvmcCallDepthExceeded = 10,
    kEvmcStaticModeViolation = 11,
    kEvmcPrecompileFailure = 12,
    kEvmcContractValidationFailure = 13,
    kEvmcArgumentOutOfRange = 14,
    kEvmcWasmUnreachableInstruction = 15,
    kEvmcWasmTrap = 16,
    kEvmcInsufficientBalance = 17,

    kEvmcInternalError = -1,
    kEvmcRejected = -2,
    kEvmcOutOfMemory = -3
};


static const uint64_t kConsensusMessageTimeoutUs = 5000000lu;
static const uint64_t kHandledTimeoutMs = 10000lu;
static const uint64_t kMessagePeriodUs = 1500000lu;
static const uint32_t kEachMessagePoolMaxCount = 2048u;

static inline std::string MessageStatusToString(MessageHandleStatus status) {
    switch (status) {
        case kConsensusSuccess:
            return "kConsensusSuccess";
        case kMessageHandle:
            return "kMessageHandle";
        case kMessageHandleError:
            return "kMessageHandleError";
        case kTxAccept:
            return "kTxAccept";
        case kTxInvalidSignature:
            return "kTxInvalidSignature";
        case kTxInvalidAddress:
            return "kTxInvalidAddress";
        case kTxPoolFullReject:
            return "kTxPoolFullReject";
        case kTxUserNonceInvalid:
            return "kTxUserNonceInvalid";
        case kUnkonwn:
            return "kUnknown";
        case kRequestInvalid:
            return "kRequestInvalid";
        case kNotExists:
            return "kNotExists";

        // --- EVMC ---
        case kEvmcFailure:
            return "kEvmcFailure";
        case kEvmcRevert:
            return "kEvmcRevert";
        case kEvmcOutOfGas:
            return "kEvmcOutOfGas";
        case kEvmcInvalidInstruction:
            return "kEvmcInvalidInstruction";
        case kEvmcUndefinedInstruction:
            return "kEvmcUndefinedInstruction";
        case kEvmcStackOverflow:
            return "kEvmcStackOverflow";
        case kEvmcStackUnderflow:
            return "kEvmcStackUnderflow";
        case kEvmcBadJumpDestination:
            return "kEvmcBadJumpDestination";
        case kEvmcInvalidMemoryAccess:
            return "kEvmcInvalidMemoryAccess";
        case kEvmcCallDepthExceeded:
            return "kEvmcCallDepthExceeded";
        case kEvmcStaticModeViolation:
            return "kEvmcStaticModeViolation";
        case kEvmcPrecompileFailure:
            return "kEvmcPrecompileFailure";
        case kEvmcContractValidationFailure:
            return "kEvmcContractValidationFailure";
        case kEvmcArgumentOutOfRange:
            return "kEvmcArgumentOutOfRange";
        case kEvmcWasmUnreachableInstruction:
            return "kEvmcWasmUnreachableInstruction";
        case kEvmcWasmTrap:
            return "kEvmcWasmTrap";
        case kEvmcInsufficientBalance:
            return "kEvmcInsufficientBalance";

        // --- EVMC  ---
        case kEvmcInternalError:
            return "kEvmcInternalError";
        case kEvmcRejected:
            return "kEvmcRejected";
        case kEvmcOutOfMemory:
            return "kEvmcOutOfMemory";

        case 5001: return "kConsensusError";
        case 5002: return "kConsensusAdded";
        case 5004: return "kConsensusNotExists";
        case 5005: return "kConsensusTxAdded";
        case 5006: return "kConsensusNoNewTxs";
        case 5007: return "kConsensusInvalidPackage";
        case 5008: return "kConsensusTxNotExists";
        case 5009: return "kConsensusAccountNotExists";
        case 5010: return "kConsensusAccountBalanceError";
        case 5011: return "kConsensusAccountExists";
        case 5012: return "kConsensusBlockHashError";
        case 5013: return "kConsensusBlockHeightError";
        case 5014: return "kConsensusPoolIndexError";
        case 5015: return "kConsensusBlockNotExists";
        case 5016: return "kConsensusBlockPreHashError";
        case 5017: return "kConsensusNetwokInvalid";
        case 5018: return "kConsensusLeaderInfoInvalid";
        case 5019: return "kConsensusExecuteContractFailed";
        case 5020: return "kConsensusGasUsedNotEqualToLeaderError";
        case 5021: return "kConsensusUserSetGasLimitError";
        case 5022: return "kConsensusCreateContractKeyError";
        case 5023: return "kConsensusContractAddressLocked";
        case 5024: return "kConsensusContractBytesCodeError";
        case 5025: return "kConsensusTimeBlockHeightError";
        case 5026: return "kConsensusElectBlockHeightError";
        case 5027: return "kConsensusLeaderTxInfoInvalid";
        case 5028: return "kConsensusVssRandomNotMatch";
        case 5029: return "kConsensusWaiting";
        case 5030: return "kConsensusOutOfGas";
        case 5031: return "kConsensusRevert";
        case 5032: return "kConsensusInvalidInstruction";
        case 5033: return "kConsensusUndefinedInstruction";
        case 5034: return "kConsensusStackOverflow";
        case 5035: return "kConsensusStackUnderflow";
        case 5036: return "kConsensusBadJumpDestination";
        case 5037: return "kConsensusInvalidMemoryAccess";
        case 5038: return "kConsensusCallDepthExceeded";
        case 5039: return "kConsensusStaticModeViolation";
        case 5040: return "kConsensusPrecompileFailure";
        case 5041: return "kConsensusContractValidationFailure";
        case 5042: return "kConsensusArgumentOutOfRange";
        case 5043: return "kConsensusWasmRnreachableInstruction";
        case 5044: return "kConsensusWasmTrap";

        // --- EVMC mapping section ---
        case 5045: return "kConsensusInsufficientBalance"; // Corresponds to original kEvmcInsufficientBalance
        case 5046: return "kConsensusInternalError";      // Corresponds to original kEvmcInternalError
        case 5047: return "kConsensusRejected";           // Corresponds to original kEvmcRejected
        case 5048: return "kConsensusOutOfMemory";        // Corresponds to original kEvmcOutOfMemory
        
        // --- Cross-shard and election logic ---
        case 5049: return "kConsensusOutOfPrefund";
        case 5050: return "kConsensusElectNodeExists";
        case 5051: return "kConsensusNonceInvalid";
        case 5052: return "kConsensusJoinElectThreashTInvalid";
        case 5053: return "kConsensusContractDestructed";
        default:
            return "unknown(" + std::to_string(static_cast<int32_t>(status)) + ")";
    }
}

// TODO: check memory
class TransportMessage {
public:
    // static std::atomic<int32_t> testTransportMessageCount;
    TransportMessage() : conn(nullptr), retry(false), 
            handled(false), is_leader(false), latest_qc_view(0llu), system_message(false) {
        timeout = common::TimeUtils::TimestampUs() + kConsensusMessageTimeoutUs;
        handle_timeout = common::kInvalidUint64;
        prev_timestamp = common::TimeUtils::TimestampMs();
// #ifndef NDEBUG
        memset(times, 0, sizeof(times));
// #endif
        times_idx = 0;
        thread_index = -1;
        // auto now_count = testTransportMessageCount.fetch_add(1);
        // SHARDORA_DEBUG("memory check create new transport message: %d", now_count);
        common::GlobalInfo::Instance()->AddSharedObj(11);
    }

    ~TransportMessage() {
        // auto now_count = testTransportMessageCount.fetch_sub(1);
        // SHARDORA_DEBUG("memory check remove transport message: %d", now_count);
        common::GlobalInfo::Instance()->DecSharedObj(11);
    }

    protobuf::Header header;
    std::string header_str;
    std::shared_ptr<tnet::TcpInterface> conn = nullptr;
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    std::string msg_hash;
    bool retry;
// #ifndef NDEBUG
    uint64_t times[64];
    std::string debug_str[64];
// #endif
    uint32_t times_idx;
    uint64_t handle_timeout;
    uint64_t timeout;
    uint64_t prev_timestamp;
    bool handled;
    bool is_leader;
    int32_t thread_index;
    uint64_t latest_qc_view;
    bool system_message;
    std::atomic<MessageHandleStatus> handle_status;

    // Optional callback invoked when set_status() is called with a terminal status.
    // Set by TxPoolManager after msg_hash is known.
    std::function<void(const std::string&, MessageHandleStatus)> status_notify_cb;

    // Set handle_status and fire status_notify_cb for terminal statuses.
    // kMessageHandle and kTxAccept are pending/success states — no notification.
    void set_status(MessageHandleStatus s) {
        handle_status = s;
        if (s == kMessageHandle || s == kTxAccept) return;
        SHARDORA_WARN("set_status: %s, hash: %s", MessageStatusToString(s).c_str(), common::Encode::HexEncode(msg_hash).c_str());
        if (status_notify_cb && !msg_hash.empty()) {
            status_notify_cb(msg_hash, s);
        }
    }
};

typedef std::shared_ptr<TransportMessage> MessagePtr;
typedef std::function<void(const transport::MessagePtr& message)> MessageProcessor;
typedef std::function<int(transport::MessagePtr& message)> FirewallCheckCallback;

class ClientItem {
public:
    ClientItem() {
        conn = nullptr;
        common::GlobalInfo::Instance()->AddSharedObj(12);
    }

    ~ClientItem() {
        common::GlobalInfo::Instance()->DecSharedObj(12);
    }

    std::string des_ip;
    uint16_t port;
    std::string msg;
    uint64_t hash64;
    uint32_t type;
    std::shared_ptr<tnet::TcpInterface> conn;
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
static const int32_t kTcpBuffLength = 10 * 1024 * 1024;

inline void CloseSocket(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

}  // namespace transport

}  // namespace shardora
