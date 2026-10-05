#include "transport/uv_tcp_transport.h"
#ifdef SHARDORA_USE_UV

#ifndef WIN32
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif
#include <cerrno>
#include <vector>
#include "common/global_info.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "transport/multi_thread.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace transport {

static common::ThreadSafeQueue<std::shared_ptr<ClientItem>>* output_queues_ = nullptr;
common::ThreadSafeQueue<transport::MessagePtr> local_messages_[common::kMaxThreadCount];
MultiThreadHandler* msg_handler_ = nullptr;

static const int kTcpBufferSize = 10 * 1024 * 1024;
using namespace tnet;
// single loop, thread safe

// Set TCP keepalive with precise intvl/count so dead peers are detected in ~7s.
// uv_tcp_keepalive() only sets TCP_KEEPIDLE; KEEPINTVL/KEEPCNT stay at OS defaults
// (typically 75s / 9 = 680s detection window) unless explicitly overridden here.
//
// Also set TCP_USER_TIMEOUT: keepalive probes are NOT sent while the socket is
// actively transmitting. HotStuff leaders keep connections "busy" with proposes,
// so half-open zombies survive forever and uv_write() still succeeds into the
// local send buffer. USER_TIMEOUT aborts when sent data is unacked for too long.
static void SetKeepaliveOpts(uv_tcp_t* handle) {
#ifndef WIN32
    uv_os_fd_t fd;
    if (uv_fileno((uv_handle_t*)handle, &fd) == 0) {
        int intvl = 2, cnt = 2;
        setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
        setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));
#ifdef TCP_USER_TIMEOUT
        unsigned int user_timeout_ms = 2000;  // 2s unacked → ETIMEDOUT (was 8s)
        setsockopt((int)fd, IPPROTO_TCP, TCP_USER_TIMEOUT,
            &user_timeout_ms, sizeof(user_timeout_ms));
#endif
    }
#endif
}

// Returns true if the connection should be discarded and reconnected.
// Outbound HotStuff sockets often never receive app data (votes arrive on a
// separate inbound connection keyed by ephemeral port), so last_recv_ts==0 is
// normal and must NOT alone force reconnect — rely on TCP_INFO / USER_TIMEOUT.
static bool IsConnectionStale(ex_uv_tcp_t* ex_uv_tcp, uint64_t now, const char** reason) {
    if (uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp) ||
            ex_uv_tcp->uv_tcp.type != UV_TCP) {
        if (reason) {
            *reason = "closing_or_bad_type";
        }
        return true;
    }

#ifndef WIN32
#ifdef TCP_INFO
    uv_os_fd_t fd;
    if (uv_fileno((uv_handle_t*)&ex_uv_tcp->uv_tcp, &fd) == 0) {
        struct tcp_info info;
        socklen_t len = sizeof(info);
        if (getsockopt((int)fd, IPPROTO_TCP, TCP_INFO, &info, &len) == 0) {
            if (info.tcpi_state != TCP_ESTABLISHED) {
                if (reason) {
                    *reason = "tcp_not_established";
                }
                return true;
            }
            // Sending but peer stopped ACKing — classic half-open zombie.
            static const uint32_t kMaxAckSilenceMs = 10000;
            if (info.tcpi_unacked > 0 && info.tcpi_last_ack_recv > kMaxAckSilenceMs) {
                if (reason) {
                    *reason = "unacked_no_peer_ack";
                }
                return true;
            }
        }
    }
#endif
#endif

    // App-level silence only applies once this socket has received data, and only
    // when nothing is waiting on an ACK. A socket with writes still in flight is
    // one the peer is expected to answer, so tearing it down on the 10s timer
    // would discard payloads that were on their way.
    static const uint64_t kMaxSilenceSec = 10;
    if (ex_uv_tcp->last_recv_ts != 0 &&
            ex_uv_tcp->unacked_bytes == 0 &&
            now > ex_uv_tcp->last_recv_ts + kMaxSilenceSec) {
        if (reason) {
            *reason = "app_silence";
        }
        return true;
    }
    return false;
}

static uv_loop_t* loop;
TcpTransport* tcp_transport = nullptr;
uv_tcp_t* socket;
uv_os_sock_t sock;
static uv_async_t async_handle;
static uv_timer_t health_timer;
std::atomic<bool> uv_transport_inited = false;
// Peers for which uv_tcp_connect is in-flight (on_connect not yet fired).
// Lives only on the libuv loop thread — no locking needed.
static std::unordered_set<std::string> pending_conns_;

// Messages that could not be handed to the kernel, kept for the next pass.
// output_queues_ is a single-producer/single-consumer queue owned by the sending
// threads, so the loop thread must never push back into it. Like the other
// transports' globals here, this is touched only by the libuv loop thread.
//
// One list rather than one per thread index: every producer here runs on the loop
// thread, with no thread-index to key on, and uv_async_cb drains every index each
// pass, so a per-index array only ever acted as a single carry list while looking
// like it had a consumer per queue.
static std::vector<std::shared_ptr<ClientItem>> deferred_items_;

// A message aimed at a peer whose uv_tcp_connect is still in flight is retried
// instead of discarded. uv_async_cb runs every ~10ms, so this bounds the wait at
// roughly 3 seconds.
static const uint32_t kMaxSendRetryTimes = 300u;

// Upper bound on the carry list. A peer that is down or flapping keeps its
// messages here while new sends keep arriving, and every one of them holds a full
// serialized payload. Without a cap the backlog grows for as long as the peer
// stays unreachable. Oldest first: the entries at the front are the ones whose
// retry budget is closest to expiring.
static const size_t kMaxDeferredItems = 10000u;

// Bounded carry list. Returns false when the cap is hit so the caller knows the
// item was dropped rather than queued.
static bool DeferItem(std::shared_ptr<ClientItem> item) {
    if (deferred_items_.size() >= kMaxDeferredItems) {
        SHARDORA_ERROR("[TCP_RECONN] carry list full (%zu), dropping msg to %s:%d hash64=%lu",
            deferred_items_.size(), item->des_ip.c_str(), item->port, item->hash64);
        return false;
    }

    deferred_items_.emplace_back(std::move(item));
    return true;
}

// Bounds on the per-connection unacked packet list. The list only has to cover
// the window between a write entering the kernel buffer and the kernel giving up
// on it — TCP_USER_TIMEOUT is 2s and the health sweep runs every 5s, so anything
// older than the last few seconds is already unrecoverable. Both caps are needed:
// a count alone allows 16 x 1.5MB, a byte cap alone allows thousands of small
// messages.
static const uint32_t kMaxUnackedPacketsPerConn = 16u;
static const uint64_t kMaxUnackedBytesPerConn = 4u * 1024u * 1024u;

// Payloads released by teardowns that did not immediately reconnect. Drained
// into the carry list at the top of every uv_async_cb pass.
static std::vector<std::shared_ptr<ClientItem>> orphaned_unacked_;

static bool TcpOutputQueuesReady(const char* context) {
    if (output_queues_ == nullptr) {
        SHARDORA_ERROR("%s: TcpTransport output queue not ready (Init not called or already torn down).", context);
        return false;
    }
    return true;
}

struct connect_ex_t {
    uv_connect_t uv_conn;
    std::string* msg;
    uint64_t hash64   = 0;
    uint32_t msg_type = 0;
    bool     is_retry = false;
    uint32_t retry_count = 0;
};

// Owns write buffer until on_write; uv_write is async so stack buffers are unsafe.
struct write_ex_t {
    uv_write_t req;
    std::string msg;         // full packet (header + payload) sent via uv_write
    std::string payload;     // raw payload only (no header), used for retry
    char des_ip[64] = {};
    uint16_t des_port = 0;
    uint64_t hash64 = 0;
    uint32_t msg_type = 0;
    bool is_retry = false;
    uint32_t retry_count = 0;
};

// Remember a payload that uv_write() reported successful but the peer has not
// acknowledged yet. Loop thread only.
static void TrackUnackedPacket(ex_uv_tcp_t* ex_uv_tcp, const write_ex_t* wr) {
    if (ex_uv_tcp == nullptr || wr == nullptr || wr->payload.empty()) {
        return;
    }

    // Inbound handles are never drained — RequeueUnackedPacketsTo skips them
    // because their port is the peer's ephemeral source port — so tracking here
    // would only accumulate until the caps evict, then trip the close-time leak
    // warning with no retry ever attempted.
    if (ex_uv_tcp->is_inbound) {
        return;
    }

    // Oldest-first eviction: the caps exist to bound memory on a wedged
    // connection, and the oldest entry is the one whose retry budget would expire
    // first anyway. Drop the oldest until one more packet fits under both caps.
    const uint64_t incoming = wr->payload.size();
    while (!ex_uv_tcp->unacked_packets.empty() &&
            (ex_uv_tcp->unacked_packets.size() >= kMaxUnackedPacketsPerConn ||
             ex_uv_tcp->unacked_bytes + incoming > kMaxUnackedBytesPerConn)) {
        ex_uv_tcp->unacked_bytes -= ex_uv_tcp->unacked_packets.front().payload.size();
        ex_uv_tcp->unacked_packets.erase(ex_uv_tcp->unacked_packets.begin());
    }

    UnackedPacket packet;
    packet.payload  = wr->payload;
    packet.hash64   = wr->hash64;
    packet.msg_type = wr->msg_type;
    packet.retry_count = wr->retry_count;
    packet.enqueued_ms = common::TimeUtils::TimestampMs();
    ex_uv_tcp->unacked_bytes += packet.payload.size();
    ex_uv_tcp->unacked_packets.emplace_back(std::move(packet));
}

// tcpi_unacked == 0 means every byte handed to the kernel has been ACKed, so
// nothing in the list needs re-sending.
//
// tcpi_state must also be checked: when the kernel aborts a socket (for example
// TCP_USER_TIMEOUT expiring on a half-open peer) it flushes the write queue, so
// tcpi_unacked drops to 0 even though the peer never received the data. Only a
// socket still in TCP_ESTABLISHED can be trusted to be reporting a genuine ACK.
//
// The age gate closes the remaining hole: tcpi_unacked counts packets the kernel
// has actually put on the wire, so a payload still queued locally because the
// peer's receive window is closed is not counted, and an empty counter would
// otherwise release it as acknowledged. A packet written microseconds ago cannot
// have been ACKed yet, so hold anything younger than the shortest plausible RTT.
static void PruneAckedPackets(ex_uv_tcp_t* ex_uv_tcp) {
    if (ex_uv_tcp == nullptr || ex_uv_tcp->unacked_packets.empty()) {
        return;
    }

#ifndef WIN32
#ifdef TCP_INFO
    uv_os_fd_t fd;
    if (uv_fileno((uv_handle_t*)&ex_uv_tcp->uv_tcp, &fd) != 0) {
        return;
    }

    struct tcp_info info;
    socklen_t len = sizeof(info);
    if (getsockopt((int)fd, IPPROTO_TCP, TCP_INFO, &info, &len) != 0) {
        return;
    }

    if (info.tcpi_state != TCP_ESTABLISHED || info.tcpi_unacked != 0) {
        // Either the socket is dead (nothing left to ACK — keep the payloads so
        // the teardown can re-queue them) or data is still in flight.
        return;
    }

    static const uint64_t kMinPruneAgeMs = 200u;
    const uint64_t now_ms = common::TimeUtils::TimestampMs();
    size_t keep_from = 0;
    while (keep_from < ex_uv_tcp->unacked_packets.size() &&
            now_ms > ex_uv_tcp->unacked_packets[keep_from].enqueued_ms + kMinPruneAgeMs) {
        ex_uv_tcp->unacked_bytes -= ex_uv_tcp->unacked_packets[keep_from].payload.size();
        ++keep_from;
    }

    if (keep_from > 0) {
        ex_uv_tcp->unacked_packets.erase(
            ex_uv_tcp->unacked_packets.begin(),
            ex_uv_tcp->unacked_packets.begin() + keep_from);
    }
#endif
#endif
}

void on_close(uv_handle_t* handle) {
    SHARDORA_DEBUG("close called: %p!", static_cast<void*>(handle));
    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)handle;
    //assert(ex_uv_tcp->msg_decoder != nullptr);
    if (ex_uv_tcp->msg_decoder) {
        delete ex_uv_tcp->msg_decoder;
        ex_uv_tcp->msg_decoder = nullptr;
    }

    // Every path that frees a handle reaches here, including the detached ones
    // (Stop(), connect-failure, network-sim drop). A non-empty list at this point
    // means the teardown path failed to re-queue the payloads it was given.
    if (!ex_uv_tcp->unacked_packets.empty()) {
        SHARDORA_ERROR("[TCP_RECONN] closing %s:%d with %zu unacked packet(s) still held, "
            "hash64 of oldest=%lu — these were not retried",
            ex_uv_tcp->ip, ex_uv_tcp->port, ex_uv_tcp->unacked_packets.size(),
            ex_uv_tcp->unacked_packets.front().hash64);
    }

    delete ex_uv_tcp;
}

void on_write(uv_write_t* req, int status) {
    write_ex_t* wr = (write_ex_t*)req;
    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)req->handle;
    if (status < 0) {
        // Write failed (broken pipe, connection reset, USER_TIMEOUT, etc.) —
        // remove the dead connection so the next Send() creates a fresh one.
        SHARDORA_WARN("[TCP_RECONN] on_write failed: %s:%d, status=%d (%s) — freeing connection",
            ex_uv_tcp->ip, ex_uv_tcp->port, status, uv_strerror(status));

        // Retry so the message is not silently lost when the connection drops
        // between Send() and the actual kernel write. This runs on the libuv loop
        // thread, so the item joins the loop-thread carry list rather than being
        // pushed back into output_queues_, which is SPSC and owned by senders.
        if (wr->retry_count < kMaxSendRetryTimes && wr->des_port > 0 && !wr->payload.empty()) {
            auto retry = std::make_shared<ClientItem>();
            retry->des_ip = std::string(wr->des_ip);
            retry->port   = wr->des_port;
            retry->msg    = wr->payload;
            retry->hash64 = wr->hash64;
            retry->type   = wr->msg_type;
            retry->is_retry = true;
            retry->retry_count = wr->retry_count + 1;
            if (DeferItem(std::move(retry))) {
                SHARDORA_WARN("[TCP_RECONN] re-queued retry %u/%u msg to %s:%d hash64=%lu",
                    wr->retry_count + 1, kMaxSendRetryTimes, wr->des_ip, wr->des_port, wr->hash64);
            }
        } else if (wr->retry_count >= kMaxSendRetryTimes) {
            SHARDORA_ERROR("[TCP_RECONN] drop msg after %u retries to %s:%d hash64=%lu",
                wr->retry_count, wr->des_ip, wr->des_port, wr->hash64);
        }

        tcp_transport->FreeConnection(ex_uv_tcp);
        // Note: Do NOT call uv_close here. FreeConnection puts the handle in invalid_conns_
        // queue, and RealFreeInvalidConnections will close it later with proper timing.
    } else {
        SHARDORA_DEBUG("[TCP_RECONN] on_write success: %s:%d", ex_uv_tcp->ip, ex_uv_tcp->port);
        // status==0 only means the bytes reached the local socket buffer. Hold the
        // payload until the peer ACKs it, so a zombie connection that discards the
        // buffer later can still be retried instead of losing the message.
        if (ex_uv_tcp->retired) {
            // The handle was torn down while this write was in flight, so nothing
            // will ever prune or drain its unacked list. Route the payload to the
            // orphan list, which uv_async_cb drains every pass.
            if (!wr->payload.empty() && wr->des_port > 0 && !ex_uv_tcp->is_inbound) {
                if (wr->retry_count >= kMaxSendRetryTimes) {
                    SHARDORA_ERROR("[TCP_RECONN] drop msg after %u retries to %s:%d hash64=%lu",
                        wr->retry_count, wr->des_ip, wr->des_port, wr->hash64);
                } else {
                    auto retry = std::make_shared<ClientItem>();
                    retry->des_ip  = std::string(wr->des_ip);
                    retry->port    = wr->des_port;
                    retry->msg     = wr->payload;
                    retry->hash64  = wr->hash64;
                    retry->type    = wr->msg_type;
                    retry->is_retry = true;
                    retry->retry_count = wr->retry_count + 1;
                    orphaned_unacked_.emplace_back(std::move(retry));
                    SHARDORA_WARN("[TCP_RECONN] write on retired connection to %s:%d completed "
                        "after teardown, hash64=%lu — re-queued", wr->des_ip, wr->des_port, wr->hash64);
                }
            }
        } else {
            TrackUnackedPacket(ex_uv_tcp, wr);
        }
    }
    delete wr;
}

class UvTcpConnection 
        : public TcpInterface, 
        public std::enable_shared_from_this<UvTcpConnection> {
public:
    UvTcpConnection(ex_uv_tcp_t* ex_uv_tcp) : ex_uv_tcp_(ex_uv_tcp) {}
    virtual ~UvTcpConnection() {}

    virtual std::string PeerIp() {
        return peer_node_public_ip_;
    }

    virtual uint16_t PeerPort() {
        return peer_node_public_port_;
    }

    virtual void SetPeerIp(const std::string& ip) {
        peer_node_public_ip_ = ip;
    }

    virtual void SetPeerPort(uint16_t port) {
        peer_node_public_port_ = port;
    }

    virtual int Send(const std::string& data) {
        return Send(data.c_str(), data.size());
    }

    virtual int Send(uint64_t msg_id, const std::string& data) {
        return Send(data.c_str(), data.size(), msg_id);
    }

    virtual int Send(const char* data, int32_t len, uint64_t msg_id) {
        //assert(false);
        return kTransportSuccess;
    }

    virtual int Send(const char* data, int32_t len) {
        //assert(false);
        return kTransportSuccess;
    }

    virtual bool Connect(uint32_t timeout) {
        //assert(false);
        return true;
    }

    virtual void Close() {

    }

    virtual void CloseWithoutLock() {

    }

    ex_uv_tcp_t* ex_uv_tcp() {
        return ex_uv_tcp_;
    }
    
private:
    std::string peer_node_public_ip_;
    uint16_t peer_node_public_port_;
    ex_uv_tcp_t* ex_uv_tcp_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(UvTcpConnection);
};

#ifdef _WIN32

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    struct sockaddr_storage ss;
    unsigned long s = size;

    memset(&ss, sizeof(ss), 0);
    ss.ss_family = af;

    switch (af) {
    case AF_INET:
        ((struct sockaddr_in *)&ss)->sin_addr = *(struct in_addr *)src;
        break;
    case AF_INET6:
        ((struct sockaddr_in6 *)&ss)->sin6_addr = *(struct in6_addr *)src;
        break;
    default:
        return NULL;
    }

    const size_t cSize = strlen(dst) + 1;
    wchar_t* wc = new wchar_t[cSize];
    mbstowcs(wc, dst, cSize);
    char* res = (WSAAddressToStringW((struct sockaddr *)&ss, sizeof(ss), NULL, wc, &s) == 0) ?
        dst : NULL;
    delete[]wc;
    return res;
}

#endif // _WIN32

bool OnClientPacket(ex_uv_tcp_t* ex_uv_tcp, tnet::Packet& packet) {
    auto& from_ip = ex_uv_tcp->ip;
    auto from_port = ex_uv_tcp->port;
    
    // 应用层网络延迟注入 (接收端) - 仅在启用时应用
    bool network_enabled = tcp_transport->GetNetworkDelaySimulator().IsEnabled();
    if (network_enabled) {
        if (tcp_transport->GetNetworkDelaySimulator().ShouldDropPacket()) {
            SHARDORA_DEBUG("[NETWORK_SIM] dropping received packet from %s:%d", from_ip, from_port);
            return false;
        }
        tcp_transport->GetNetworkDelaySimulator().ApplyDelay();
    }
    
    tnet::MsgPacket* msg_packet = dynamic_cast<tnet::MsgPacket*>(&packet);
    char* data = nullptr;
    uint32_t len = 0;
    msg_packet->GetMessageEx(&data, &len);
    if (data == nullptr) {
        SHARDORA_DEBUG("data == nullptr");
        return false;
    }

    // Reject oversized packets — use 150% of kMaxProposeMsgBytes to allow some headroom
    // for headers, signatures, and protobuf overhead on top of the propose payload.
    static const uint32_t kMaxPacketBytes = (uint32_t)(common::kMaxProposeMsgBytes * 3 / 2);
    if (len == 0 || len > kMaxPacketBytes) {
        SHARDORA_WARN("[PACKET_VALIDATION] oversized or empty packet from %s:%d, len=%u (max=%u) — closing connection",
                  from_ip, from_port, len, kMaxPacketBytes);
        // Return false to signal caller (on_read) to close this connection
        return false;
    }

    MessagePtr msg_ptr = std::make_shared<TransportMessage>();
    if (!msg_ptr->header.ParseFromArray(data, len)) {
        SHARDORA_ERROR("Message ParseFromString from string failed!"
            "[%s:%d][len: %d]",
            from_ip, from_port, len);
        return false;  // caller closes connection
    }

    if (msg_ptr->header.has_broadcast()) {
        msg_ptr->header_str = std::string(data, len);
    }

    if (msg_ptr->header.has_from_public_port() &&
            msg_ptr->header.from_public_port() != 0) {
        from_port = msg_ptr->header.from_public_port();
    }

    msg_ptr->conn = std::make_shared<UvTcpConnection>(ex_uv_tcp);
    msg_ptr->conn->SetPeerIp(from_ip);
    msg_ptr->conn->SetPeerPort(from_port);
    if (from_port <= 0) {
        SHARDORA_ERROR("message coming: %s:%d, type: %d, invalid port", from_ip, from_port, msg_ptr->header.type());
        return false;
    }

    tcp_transport->msg_handler()->HandleMessage(msg_ptr);
    return true;
}

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    *buf = uv_buf_init((char*)malloc(size), size);
}

void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
    SHARDORA_DEBUG("get client data: %d", nread);
    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)tcp;
    if (nread >= 0) {
        ex_uv_tcp->last_recv_ts = common::TimeUtils::TimestampSeconds();
        // Every byte handed to the kernel has been ACKed, so nothing here needs
        // re-sending. Checked before staleness so the acked packets are released
        // rather than carried into a teardown that would re-queue them.
        PruneAckedPackets(ex_uv_tcp);

        ex_uv_tcp->msg_decoder->Decode(buf->base, nread);
        auto packet = ex_uv_tcp->msg_decoder->GetPacket();
        SHARDORA_DEBUG("get packet data: %d", (packet != nullptr));
        while (packet != nullptr) {
            bool ok = OnClientPacket(ex_uv_tcp, *packet);
            packet->Free();
            if (!ok) {
                // Bad packet (parse error, oversized, invalid port, or dropped by network sim)
                // Close the connection and let next send create a fresh one
                free(buf->base);
                SHARDORA_WARN("[TCP_RECONN] on_read: bad packet from %s:%d — freeing connection",
                    ex_uv_tcp->ip, ex_uv_tcp->port);
                tcp_transport->FreeConnection(ex_uv_tcp);
                return;
            }
            packet = ex_uv_tcp->msg_decoder->GetPacket();
        }
    } else {
        // Connection error (EOF, reset, timeout, etc.)
        SHARDORA_WARN("[TCP_RECONN] on_read error: %s:%d, nread=%zd (%s) — freeing connection",
            ex_uv_tcp->ip, ex_uv_tcp->port, nread, uv_strerror(nread));
        tcp_transport->FreeConnection(ex_uv_tcp);
    }

    free(buf->base);
}

void on_connect(uv_connect_t* connection, int status) {
    uv_stream_t* stream = connection->handle;
    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)stream;
    // Always unblock the pending slot so the next send can retry.
    std::string peer_key = std::string(ex_uv_tcp->ip) + ":" + std::to_string(ex_uv_tcp->port);
    pending_conns_.erase(peer_key);

    if (status < 0) {
        SHARDORA_WARN("[TCP_RECONN] failed to connect %s:%d, status=%d (%s) — will retry on next send",
            ex_uv_tcp->ip, ex_uv_tcp->port, status, uv_strerror(status));
        connect_ex_t* ex_conn = (connect_ex_t*)connection;
        // Re-queue the attached message so it is not silently lost.
        // Strip the PacketHeader prefix that was prepended in uv_async_cb.
        // Loop-thread callback: the item goes on the carry list, not back into the
        // sender-owned SPSC queue.
        if (ex_conn->msg && ex_conn->msg->size() > sizeof(PacketHeader) &&
                ex_conn->retry_count < kMaxSendRetryTimes) {
            auto retry = std::make_shared<ClientItem>();
            retry->des_ip   = std::string(ex_uv_tcp->ip);
            retry->port     = ex_uv_tcp->port;
            retry->msg      = ex_conn->msg->substr(sizeof(PacketHeader));
            retry->hash64   = ex_conn->hash64;
            retry->type     = ex_conn->msg_type;
            retry->is_retry = true;
            retry->retry_count = ex_conn->retry_count + 1;
            if (DeferItem(std::move(retry))) {
                SHARDORA_WARN("[TCP_RECONN] connect failed, re-queued msg %u/%u to %s:%d hash64=%lu",
                    ex_conn->retry_count + 1, kMaxSendRetryTimes,
                    ex_uv_tcp->ip, ex_uv_tcp->port, ex_conn->hash64);
            }
        } else if (ex_conn->msg && ex_conn->retry_count >= kMaxSendRetryTimes) {
            SHARDORA_ERROR("[TCP_RECONN] drop msg after %u retries to %s:%d hash64=%lu",
                ex_conn->retry_count, ex_uv_tcp->ip, ex_uv_tcp->port, ex_conn->hash64);
        }
        delete ex_conn->msg;
        free(ex_conn);
        uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
        return;
    }

    SHARDORA_DEBUG("[TCP_RECONN] successfully connected to %s:%d", ex_uv_tcp->ip, ex_uv_tcp->port);

    // Increase buffer sizes for high-throughput scenarios
    int new_recv_size = 20 * 1024 * 1024;  // 20MB (from 10MB)
    uv_recv_buffer_size((uv_handle_t*)stream, &new_recv_size);
    int new_send_size = 20 * 1024 * 1024;  // 20MB (from 10MB)
    uv_send_buffer_size((uv_handle_t*)stream, &new_send_size);
    
    // Enable TCP keepalive: idle=3s, intvl=2s, cnt=2 → dead peer detected in ~7s
    // + TCP_USER_TIMEOUT for active-send zombie detection
    uv_tcp_keepalive(&ex_uv_tcp->uv_tcp, 1, 3);
    SetKeepaliveOpts(&ex_uv_tcp->uv_tcp);
    // Disable Nagle's algorithm for lower latency
    uv_tcp_nodelay(&ex_uv_tcp->uv_tcp, 1);

    connect_ex_t* ex_conn = (connect_ex_t*)connection;
    write_ex_t* wr = new write_ex_t();
    wr->msg = std::move(*ex_conn->msg);
    // Carry the retry metadata across. Without it on_write() sees des_port==0 and
    // an empty payload, so a failure on this first write takes the "nothing to
    // retry" branch and the message disappears with no drop logged.
    wr->payload = wr->msg.substr(sizeof(PacketHeader));
    strncpy(wr->des_ip, ex_uv_tcp->ip, sizeof(wr->des_ip) - 1);
    wr->des_port = ex_uv_tcp->port;
    wr->hash64 = ex_conn->hash64;
    wr->msg_type = ex_conn->msg_type;
    wr->is_retry = ex_conn->is_retry;
    wr->retry_count = ex_conn->retry_count;
    delete ex_conn->msg;
    ex_conn->msg = nullptr;
    uv_buf_t uv_buf = uv_buf_init(const_cast<char*>(wr->msg.data()), wr->msg.size());
    
    // 应用层网络延迟注入 - 仅在启用时应用
    // 在高并发压测下，延迟注入可能导致包头破坏，建议禁用
    bool network_enabled = tcp_transport->GetNetworkDelaySimulator().IsEnabled();
    if (network_enabled) {
        if (tcp_transport->GetNetworkDelaySimulator().ShouldDropPacket()) {
            SHARDORA_DEBUG("[NETWORK_SIM] dropping packet on connect to %s:%d",
                ex_uv_tcp->ip, ex_uv_tcp->port);
            delete wr;
            free(ex_conn);
            uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
            return;
        }
        // Apply delay after connection is established but before sending
        tcp_transport->GetNetworkDelaySimulator().ApplyDelay();
    }
    
    uv_write(&wr->req, (uv_stream_t*)&ex_uv_tcp->uv_tcp, &uv_buf, 1, on_write);
    free(ex_conn);
    ex_uv_tcp->connect_ts = common::TimeUtils::TimestampSeconds();
    uv_read_start((uv_stream_t*)&ex_uv_tcp->uv_tcp, alloc_cb, on_read); 
    tcp_transport->AddConnection(ex_uv_tcp);
}

void alloc_buffer(uv_handle_t*, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested_size);
    buf->len = suggested_size;
}

void on_new_connection(uv_stream_t* server, int status) {
    if (status < 0) {
        SHARDORA_DEBUG("connection failed: %s", uv_strerror(status));
        return;
    }

    ex_uv_tcp_t* ex_uv_tcp = new ex_uv_tcp_t();
    uv_tcp_init(loop, &ex_uv_tcp->uv_tcp);
    ex_uv_tcp->uv_tcp.data = ex_uv_tcp;
    ex_uv_tcp->msg_decoder = new MsgDecoder();
    if (uv_accept(server, (uv_stream_t*)&ex_uv_tcp->uv_tcp) == 0) {
        int new_recv_size = kTcpBufferSize;
        uv_recv_buffer_size((uv_handle_t *)&ex_uv_tcp->uv_tcp, &new_recv_size);
        int new_send_size = kTcpBufferSize;
        uv_send_buffer_size((uv_handle_t *)&ex_uv_tcp->uv_tcp, &new_send_size);
        // Enable TCP keepalive: idle=3s, intvl=2s, cnt=2 → dead peer detected in ~7s
        // + TCP_USER_TIMEOUT for active-send zombie detection
        uv_tcp_keepalive(&ex_uv_tcp->uv_tcp, 1, 3);
        SetKeepaliveOpts(&ex_uv_tcp->uv_tcp);
        
        struct sockaddr_storage peername;
        int namelen = sizeof(peername);
        uv_tcp_getpeername(&ex_uv_tcp->uv_tcp, (struct sockaddr*)&peername, &namelen);
        struct sockaddr_in* addr = (struct sockaddr_in*)&peername;
        uv_inet_ntop(AF_INET, &addr->sin_addr, ex_uv_tcp->ip, sizeof(ex_uv_tcp->ip));
        ex_uv_tcp->port = ntohs(addr->sin_port);
        // This port is the peer's ephemeral source port, not something we can
        // dial back — retrying a failed write on this handle would connect to
        // whatever process happens to own that port, so teardown must not
        // re-queue payloads that were written here.
        ex_uv_tcp->is_inbound = true;
        ex_uv_tcp->connect_ts = common::TimeUtils::TimestampSeconds();
        SHARDORA_DEBUG("new connection: %s:%d", ex_uv_tcp->ip, ex_uv_tcp->port);
        uv_read_start((uv_stream_t*)&ex_uv_tcp->uv_tcp, alloc_buffer, on_read);
        tcp_transport->AddConnection(ex_uv_tcp);
    } else {
        // uv_accept failed: close and free the handle.
        // h->data is already set to ex_uv_tcp above, so the callback is safe.
        uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, [](uv_handle_t* h) {
            auto tmp = reinterpret_cast<ex_uv_tcp_t*>(h->data);
            if (tmp) {
                delete tmp->msg_decoder;
                delete tmp;
            }
        });
    }
}


void signal_handler(uv_signal_t* handle, int signum) {
    SHARDORA_WARN("uv tcp server signal coming: %d", signum);
    uv_signal_stop(handle);
    uv_walk(loop, [](uv_handle_t* h, void*) {
        if (!uv_is_closing(h)) {
            if (uv_handle_get_type(h) == UV_TCP) {
                // Only UV_TCP handles carry an ex_uv_tcp_t in data.
                uv_close(h, [](uv_handle_t* ch) {
                    auto tmp = reinterpret_cast<ex_uv_tcp_t*>(ch->data);
                    if (tmp) {
                        delete tmp->msg_decoder;
                        tmp->msg_decoder = nullptr;
                        delete tmp;
                    }
                });
            } else {
                // Signal, async, and other handle types: close without freeing ex_uv_tcp_t.
                uv_close(h, nullptr);
            }
        }
    }, nullptr);
}

TcpTransport* TcpTransport::Instance() {
    static TcpTransport ins;
    return &ins;
}

TcpTransport::TcpTransport() {
    tcp_transport = this;
}

TcpTransport::~TcpTransport() {}

int TcpTransport::Init(
        const std::string& ip_port,
        int backlog, 
        bool create_server, 
        MultiThreadHandler* msg_handler) {
    output_queues_ = new common::ThreadSafeQueue<std::shared_ptr<ClientItem>>[common::kMaxThreadCount];
    ip_port_ = ip_port;
    backlog_ = backlog;
    create_server_ = create_server;
    msg_handler_ = msg_handler;
    loop = uv_default_loop();
    msg_random_ = common::Random::RandomString(32);
    return kTransportSuccess;
}

MultiThreadHandler* TcpTransport::msg_handler() {
    return msg_handler_;
}

int TcpTransport::Start(bool hold) {
    if (hold) {
        Run();
    } else {
        run_thread_ = std::make_shared<std::thread>(std::bind(&TcpTransport::Run, this));
        // Do NOT detach — we need to join in Stop() for clean shutdown.
    }

    return kTransportSuccess;
}

void TcpTransport::Stop() {
    if (destroy_) {
        return;
    }

    destroy_ = true;

    // Signal the Output() thread to exit and wake it up.
    output_con_.notify_all();
    if (output_thread_ != nullptr && output_thread_->joinable()) {
        output_thread_->join();
        output_thread_ = nullptr;
    }

    // Signal the libuv loop to stop, then wait for Run() to exit.
    // uv_stop() is safe to call from any thread.
    if (loop != nullptr) {
        // Stop health timer from the loop thread via async + uv_stop.
        // uv_timer_stop is not always safe cross-thread; uv_stop drains the loop.
        uv_stop(loop);
        // Also send an async wakeup in case the loop is blocked waiting for I/O.
        uv_async_send(&async_handle);
    }

    if (run_thread_ != nullptr && run_thread_->joinable()) {
        run_thread_->join();
        run_thread_ = nullptr;
    }

    // Now it is safe to close the loop — Run() has exited.
    if (loop != nullptr) {
        uv_loop_close(loop);
    }

    // Release the carry lists here rather than leaving them to static destruction:
    // each element is a shared_ptr whose destructor touches common::GlobalInfo, and
    // the order in which file-scope destructors run at exit is unspecified, so a
    // late drop could fire after GlobalInfo is gone. The loop thread has been
    // joined above, so nothing can be appending at this point.
    {
        size_t abandoned = deferred_items_.size() + orphaned_unacked_.size();
        if (abandoned > 0) {
            SHARDORA_WARN("[TCP_RECONN] transport stopping with %zu undelivered message(s) in the carry lists",
                abandoned);
        }
        deferred_items_.clear();
        orphaned_unacked_.clear();
    }

    if (output_queues_ != nullptr) {
        delete[] output_queues_;
        output_queues_ = nullptr;
    }
}

uint8_t TcpTransport::GetThreadIndexWithPool(uint32_t pool_index) {
    return msg_handler_->GetThreadIndexWithPool(pool_index);
}

int TcpTransport::Send(
        std::shared_ptr<tnet::TcpInterface> conn,
        const transport::protobuf::Header& message) {
    if (!TcpOutputQueuesReady("TcpTransport::Send(conn,Header)")) {
        return kTransportError;
    }
    auto output_item = std::make_shared<ClientItem>();
    output_item->conn = conn;
    output_item->type = message.type();
    output_item->hash64 = message.hash64();
    message.SerializeToString(&output_item->msg);
    if (output_item->msg.size() >= (uint32_t)(common::kMaxProposeMsgBytes * 3 / 2)) {
        SHARDORA_ERROR("dropping oversized msg (conn): size=%zu, type=%d", output_item->msg.size(), message.type());
        return kTransportError;
    }
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    output_queues_[thread_idx].push(output_item);
    output_con_.notify_one();
    return kTransportSuccess;
}

int TcpTransport::Send(
        std::shared_ptr<tnet::TcpInterface> conn,
        const std::string& message) {
    if (!TcpOutputQueuesReady("TcpTransport::Send(conn,string)")) {
        return kTransportError;
    }
    auto output_item = std::make_shared<ClientItem>();
    output_item->conn = conn;
    output_item->hash64 = 0;
    output_item->msg = message;
    if (output_item->msg.size() >= (uint32_t)(common::kMaxProposeMsgBytes * 3 / 2)) {
        SHARDORA_ERROR("dropping oversized msg (conn/str): size=%zu", output_item->msg.size());
        return kTransportError;
    }
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    output_queues_[thread_idx].push(output_item);
    output_con_.notify_one();
    return kTransportSuccess;
}
    
int TcpTransport::Send(
        const std::string& des_ip,
        uint16_t des_port,
        transport::protobuf::Header& message) {
    //assert(des_port > 0);
    if (!TcpOutputQueuesReady("TcpTransport::Send(ip,port,Header)")) {
        return kTransportError;
    }
    auto tmpHeader = const_cast<transport::protobuf::Header*>(&message);
    tmpHeader->set_from_public_port(common::GlobalInfo::Instance()->config_public_port());
    // //assert(message.broadcast().bloomfilter_size() < 64);
    if (!message.has_hash64() || message.hash64() == 0) {
        SetMessageHash(message);
    }

    auto output_item = std::make_shared<ClientItem>();
    output_item->des_ip = des_ip;
    output_item->port = des_port;
    output_item->type = message.type();
    output_item->hash64 = message.hash64();
    message.SerializeToString(&output_item->msg);
    if (output_item->msg.size() >= (uint32_t)(common::kMaxProposeMsgBytes * 3 / 2)) {
        SHARDORA_ERROR("dropping oversized msg (ip): size=%zu, type=%d, des=%s:%d",
            output_item->msg.size(), message.type(), des_ip.c_str(), des_port);
        return kTransportError;
    }
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    output_queues_[thread_idx].push(output_item);
    output_con_.notify_one();
    SHARDORA_DEBUG("success add sent out message des: %s, %d, hash64: %lu", des_ip.c_str(),des_port, message.hash64());
    return kTransportSuccess;
}

int TcpTransport::Send(
        const std::string& des_ip,
        uint16_t des_port,
        uint32_t type,
        uint64_t hash64,
        std::string&& serialized_msg) {
    if (!TcpOutputQueuesReady("TcpTransport::Send(ip,port,serialized)")) {
        return kTransportError;
    }
    if (serialized_msg.size() >= (uint32_t)(common::kMaxProposeMsgBytes * 3 / 2)) {
        SHARDORA_ERROR("dropping oversized pre-serialized msg: size=%zu, type=%u, des=%s:%d",
            serialized_msg.size(), type, des_ip.c_str(), des_port);
        return kTransportError;
    }
    auto output_item = std::make_shared<ClientItem>();
    output_item->des_ip = des_ip;
    output_item->port = des_port;
    output_item->type = type;
    output_item->hash64 = hash64;
    output_item->msg = std::move(serialized_msg);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    output_queues_[thread_idx].push(output_item);
    output_con_.notify_one();
    SHARDORA_DEBUG("success add pre-serialized msg des: %s, %d, hash64: %lu", des_ip.c_str(), des_port, hash64);
    return kTransportSuccess;
}

void TcpTransport::Output() {
    while (!destroy_) {
        uv_async_send(&async_handle);
        std::unique_lock<std::mutex> lock(output_mutex_);
        output_con_.wait_for(lock, std::chrono::milliseconds(10));
    }
}

void TcpTransport::AddLocalMessage(transport::MessagePtr msg_ptr) {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    local_messages_[thread_idx].push(msg_ptr);
    if (!uv_transport_inited) {
        return;
    }
    
    uv_async_send(&async_handle);
}

void uv_async_cb(uv_async_t* handle) {
    tcp_transport->RealFreeInvalidConnections();

    // Payloads released by teardowns that did not immediately reconnect. Folded
    // into the carry list before the drain below so they are retried this pass
    // rather than waiting for the next one.
    if (!orphaned_unacked_.empty()) {
        for (auto& item : orphaned_unacked_) {
            DeferItem(std::move(item));
        }
        orphaned_unacked_.clear();
    }

    // Messages that could not be handed to the kernel during the previous pass.
    // output_queues_ is a single-producer/single-consumer queue owned by the
    // sending thread, so the loop thread must not push back into it. Swapped out
    // once, before the walk over the queue indices: items deferred again during
    // this pass must wait for the next pass, not be retried another 31 times
    // inside this one.
    std::vector<std::shared_ptr<ClientItem>> retry_items;
    retry_items.swap(deferred_items_);
    // Drained front-to-back. A stack would reverse the order, and with several
    // messages to one peer that means sending a higher nonce before a lower one.
    size_t retry_idx = 0;

    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        MessagePtr msg_ptr;
        while (local_messages_[i].pop(&msg_ptr)) {
            msg_handler_->HandleMessage(msg_ptr);
        }

        while (true) {
            std::shared_ptr<ClientItem> item_ptr = nullptr;
            if (retry_idx < retry_items.size()) {
                item_ptr = std::move(retry_items[retry_idx++]);
            } else if (!output_queues_[i].pop(&item_ptr)) {
                break;
            }

            auto& des_ip = item_ptr->des_ip;
            auto des_port = item_ptr->port;
            ex_uv_tcp_t* ex_uv_tcp = nullptr;
            // if (item_ptr->conn) {
            //     ex_uv_tcp = std::dynamic_pointer_cast<UvTcpConnection>(item_ptr->conn)->ex_uv_tcp();
            //     if (ex_uv_tcp != nullptr) {
            //         if (uv_is_closing((uv_handle_t*)ex_uv_tcp->uv_tcp)) {
            //             ex_uv_tcp = nullptr;
            //         } else {
            //             if (ex_uv_tcp->uv_tcp->type != UV_TCP) {
            //                 ex_uv_tcp = nullptr;
            //             }
            //         }
            //     }
            // }
            
            if (ex_uv_tcp == nullptr) {
                ex_uv_tcp = transport::TcpTransport::Instance()->GetConnection(des_ip, des_port);
                if (ex_uv_tcp != nullptr) {
                    const char* stale_reason = nullptr;
                    uint64_t now = common::TimeUtils::TimestampSeconds();
                    bool stale = IsConnectionStale(ex_uv_tcp, now, &stale_reason);
                    if (stale) {
                        SHARDORA_WARN("[TCP_RECONN] stale connection detected: %s:%d "
                            "(reason=%s, last_recv=%lu, closing=%d, type=%d) — reconnecting",
                            des_ip.c_str(), des_port,
                            stale_reason ? stale_reason : "unknown",
                            ex_uv_tcp->last_recv_ts,
                            uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp),
                            (int)ex_uv_tcp->uv_tcp.type);
                        transport::TcpTransport::Instance()->FreeConnection(ex_uv_tcp);
                        ex_uv_tcp = nullptr;
                    } else {
                        SHARDORA_DEBUG("[TCP_RECONN] reusing existing connection: %s:%d %p",
                            des_ip.c_str(), des_port, static_cast<void*>(&ex_uv_tcp->uv_tcp));
                    }
                }
            }

            if (ex_uv_tcp == nullptr) {
                // If a connect is already in-flight for this peer, skip creating
                // another one — it would race with on_connect and either duplicate-
                // send or cause the first connection to be torn down on AddConnection.
                std::string peer_key = des_ip + ":" + std::to_string(des_port);
                if (pending_conns_.count(peer_key)) {
                    // The connect for this peer has not completed yet. Hold the
                    // message rather than dropping it — a burst of sends to one
                    // peer all land here while the first one is still connecting.
                    if (++item_ptr->retry_count > kMaxSendRetryTimes) {
                        SHARDORA_ERROR("[TCP_RECONN] drop msg after %u retries, connect still pending for %s, hash64=%lu",
                            item_ptr->retry_count, peer_key.c_str(), item_ptr->hash64);
                        continue;
                    }
                    // DeferItem logs and returns false at the cap; the item is
                    // dropped either way, so the two branches end the same way.
                    DeferItem(std::move(item_ptr));
                    continue;
                }

                ex_uv_tcp_t* ex_uv_tcp = new ex_uv_tcp_t();
                uv_tcp_init(loop, &ex_uv_tcp->uv_tcp);
                struct sockaddr_in server_addr;
                uv_ip4_addr(des_ip.c_str(), des_port, &server_addr);
                connect_ex_t* ex_conn = (connect_ex_t*)malloc(sizeof(connect_ex_t));
                memset(ex_conn, 0, sizeof(connect_ex_t));
                std::string* msg = new std::string();
                PacketHeader header(item_ptr->msg.size(), 0);
                msg->append((char*)&header, sizeof(header));
                msg->append(item_ptr->msg);
                ex_conn->msg      = msg;
                ex_conn->hash64   = item_ptr->hash64;
                ex_conn->msg_type = item_ptr->type;
                ex_conn->is_retry = item_ptr->is_retry;
                ex_conn->retry_count = item_ptr->retry_count;
                ex_uv_tcp->msg_decoder = new MsgDecoder();
                memcpy(ex_uv_tcp->ip, des_ip.c_str(), des_ip.size());
                ex_uv_tcp->port = des_port;
                SHARDORA_DEBUG("now connect to server: %s:%d, hash64: %lu",
                    des_ip.c_str(), des_port, item_ptr->hash64);
                int res = uv_tcp_connect(
                    (uv_connect_t*)&ex_conn->uv_conn,
                    (uv_tcp_t*)&ex_uv_tcp->uv_tcp,
                    (const struct sockaddr*)&server_addr,
                    on_connect);
                if (res < 0) {
                    SHARDORA_ERROR("[TCP_RECONN] failed to initiate connect to %s:%d, res=%d (%s), hash64=%lu",
                        des_ip.c_str(), des_port, res, uv_strerror(res), item_ptr->hash64);
                    delete msg;
                    delete ex_uv_tcp->msg_decoder;
                    delete ex_uv_tcp;
                    free(ex_conn);
                    if (++item_ptr->retry_count <= kMaxSendRetryTimes) {
                        DeferItem(std::move(item_ptr));
                    }
                } else {
                    pending_conns_.insert(peer_key);
                    SHARDORA_DEBUG("[TCP_RECONN] initiated connect to %s:%d, hash64=%lu",
                        des_ip.c_str(), des_port, item_ptr->hash64);
                }
            } else {
                write_ex_t* wr = new write_ex_t();
                PacketHeader header(item_ptr->msg.size(), 0);
                wr->msg.append((char*)&header, sizeof(header));
                wr->msg.append(item_ptr->msg);
                // Save retry metadata so on_write failure can re-queue the message.
                wr->payload  = item_ptr->msg;
                strncpy(wr->des_ip, des_ip.c_str(), sizeof(wr->des_ip) - 1);
                wr->des_port = des_port;
                wr->hash64   = item_ptr->hash64;
                wr->msg_type = item_ptr->type;
                wr->is_retry = item_ptr->is_retry;
                wr->retry_count = item_ptr->retry_count;
                uv_buf_t buf = uv_buf_init(const_cast<char*>(wr->msg.data()), wr->msg.size());
                if (item_ptr->type == common::kHotstuffMessage) {
                    SHARDORA_DEBUG("[TCP_RECONN] sending to existing connection: %s:%d, hash64=%lu",
                        des_ip.c_str(), des_port, item_ptr->hash64);
                }
                
                // 应用层网络延迟注入 - 仅在启用时应用
                bool network_enabled = transport::TcpTransport::Instance()->GetNetworkDelaySimulator().IsEnabled();
                if (network_enabled) {
                    if (transport::TcpTransport::Instance()->GetNetworkDelaySimulator().ShouldDropPacket()) {
                        SHARDORA_DEBUG("[NETWORK_SIM] dropping packet to %s:%d", 
                            des_ip.c_str(), des_port);
                        delete wr;
                        continue;
                    }
                    transport::TcpTransport::Instance()->GetNetworkDelaySimulator().ApplyDelay();
                }
                
                int wr_res = uv_write(&wr->req, (uv_stream_t*)&ex_uv_tcp->uv_tcp, &buf, 1, on_write);
                if (wr_res < 0) {
                    SHARDORA_WARN("[TCP_RECONN] uv_write failed immediately: %s:%d, res=%d (%s), "
                        "hash64=%lu — freeing connection",
                        des_ip.c_str(), des_port, wr_res, uv_strerror(wr_res), item_ptr->hash64);
                    delete wr;
                    transport::TcpTransport::Instance()->FreeConnection(ex_uv_tcp);
                    if (++item_ptr->retry_count <= kMaxSendRetryTimes) {
                        DeferItem(std::move(item_ptr));
                    }
                }
            }
        }
    }
}

int TcpTransport::SendToLocal(transport::protobuf::Header& message) {
    return kTransportSuccess;
}

int TcpTransport::GetSocket() {
    return kTransportSuccess;
}

void TcpTransport::Run() {
#ifndef WIN32
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGPIPE);
    int rc = pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
    if (rc != 0) {
        printf("block sigpipe error/n");
    }
#endif

    uv_tcp_t server;
    uv_tcp_init(loop, &server);
    struct sockaddr_in addr;
    common::Split<> splits(ip_port_.c_str(), ':');
    if (splits.Count() != 2) {
        SHARDORA_FATAL("invalid ip port: %s", ip_port_.c_str());
        return;
    }

    uint16_t port = 0;
    if (!common::StringUtil::ToUint16(splits[1], &port)) {
        SHARDORA_FATAL("invalid ip port: %s", ip_port_.c_str());
        return;
    }

    // SO_REUSEADDR must be set on the raw fd BEFORE uv_tcp_bind to avoid TIME_WAIT failures.
    //
    // SO_REUSEPORT is deliberately NOT set. Data normally travels over an
    // established connection, but this transport also dials a peer's well-known
    // port, and such a connect lands on whichever process the kernel picks.
    // SO_REUSEPORT lets a second, stale process bind the same port "successfully"
    // instead of failing, and the kernel then splits incoming connections between
    // the two — so the receiver that gets the message is not necessarily the peer.
    // Every failure below returns instead of falling through: without the port
    // there is no transport, and continuing would leave the node listening on
    // nothing while the retry loop below masks the reason.
    {
        uv_os_sock_t raw_sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (raw_sock == -1) {
            SHARDORA_ERROR("create listen socket failed: %s: %d, errno=%d",
                splits[0], port, errno);
            return;
        }

        int opt = 1;
        if (setsockopt(raw_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
            SHARDORA_ERROR("setsockopt SO_REUSEADDR failed: %s: %d, errno=%d",
                splits[0], port, errno);
            CloseSocket(raw_sock);
            return;
        }

        if (uv_tcp_open(&server, raw_sock) != 0) {
            SHARDORA_ERROR("uv_tcp_open failed: %s: %d", splits[0], port);
            CloseSocket(raw_sock);
            return;
        }
    }

    uv_ip4_addr(splits[0], port, &addr);
    int bind_res = uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);
    if (bind_res < 0) {
        // Typically EADDRINUSE. Fatal for this transport: retrying uv_listen
        // cannot make a port free, it only hides the conflict.
        SHARDORA_ERROR("bind failed: %s: %d, res: %s (%d) — transport unavailable, not listening",
            splits[0], port, uv_strerror(bind_res), bind_res);
        return;
    }

    int32_t try_times = 0;
    do {
        int r = uv_listen((uv_stream_t*)&server, 128, on_new_connection);
        if (r == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (uv_is_active((uv_handle_t*)&server)) {
                break;
            }

            SHARDORA_FATAL("listen failed: %s: %d, server inactive.", splits[0], port);
            return;
        }

        SHARDORA_ERROR("listen failed: %s: %d, res: %s, retry %d/60", splits[0], port, uv_strerror(r), try_times);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } while (try_times++ < 60);

    if (try_times >= 60) {
        SHARDORA_FATAL("listen failed: %s: %d", splits[0], port);
        return;
    }
    
    uv_signal_t sig;
    uv_signal_init(loop, &sig);
    uv_signal_start(&sig, signal_handler, SIGINT);
    SHARDORA_DEBUG("init uv tcp transport success: %s", ip_port_.c_str());
    uv_async_init(loop, &async_handle, uv_async_cb);
    uv_timer_init(loop, &health_timer);
    uv_timer_start(&health_timer, [](uv_timer_t*) {
        if (tcp_transport != nullptr) {
            tcp_transport->CheckConnectionsHealth();
        }
    }, 5000, 5000);
    output_thread_ = std::make_shared<std::thread>(&TcpTransport::Output, this);
    uv_transport_inited = true;
    while (!destroy_) {
        if (uv_run(loop, UV_RUN_DEFAULT) != 0) {
            SHARDORA_ERROR("uv run failed!");
        }

        if (!destroy_) {
            std::this_thread::sleep_for(std::chrono::microseconds(10000ull));
        }
    }
    // uv_loop_close is called by Stop() after joining this thread.
}

ex_uv_tcp_t* TcpTransport::GetConnection(const std::string& ip, uint16_t port) {
    std::string peer_spec = ip + ":" + std::to_string(port);
    auto iter = conn_map_.find(peer_spec);
    if (iter != conn_map_.end()) {
        SHARDORA_DEBUG("[TCP_RECONN] GetConnection: found existing connection %s:%d %p",
            ip.c_str(), port, static_cast<void*>(&iter->second->uv_tcp));
        return iter->second;
    }

    SHARDORA_DEBUG("[TCP_RECONN] GetConnection: no existing connection for %s:%d, will create new",
        ip.c_str(), port);
    return nullptr;
}

void TcpTransport::RealFreeInvalidConnections() {
    auto now_sec = common::TimeUtils::TimestampSeconds();
    while (!invalid_conns_.empty()) {
        auto* ex_uv_tcp = invalid_conns_.front();
        // Keep recently-freed connections in the queue for a grace period
        // to allow in-flight callbacks to complete before closing the handle.
        if (now_sec < ex_uv_tcp->timeout + kInvalidConnectionTimeoutSec) {
            break;  // Queue is ordered by timeout — all remaining are newer
        }

        // Grace period expired — safe to close the handle now
        invalid_conns_.pop();
        if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
            uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
        }
    }
}

void TcpTransport::CheckConnectionsHealth() {
    if (destroy_) {
        return;
    }
    RealFreeInvalidConnections();
    uint64_t now = common::TimeUtils::TimestampSeconds();
    std::vector<ex_uv_tcp_t*> stale_conns;
    stale_conns.reserve(conn_map_.size());
    for (auto& kv : conn_map_) {
        // A connection that is mostly a sender may never see on_read fire, so the
        // list would never be pruned from there and would pin the payloads (and
        // block the app_silence branch below) indefinitely. Pruning here is safe
        // because PruneAckedPackets only trusts a socket that TCP_INFO still
        // reports as TCP_ESTABLISHED with nothing unacknowledged.
        PruneAckedPackets(kv.second);
        const char* reason = nullptr;
        if (IsConnectionStale(kv.second, now, &reason)) {
            SHARDORA_DEBUG("[TCP_RECONN] health sweep: stale %s (reason=%s, last_recv=%lu) — freeing",
                kv.first.c_str(), reason ? reason : "unknown", kv.second->last_recv_ts);
            stale_conns.push_back(kv.second);
        }
    }
    for (auto* conn : stale_conns) {
        FreeConnection(conn);
    }
}

// A connection is being torn down. Anything uv_write() accepted but the peer
// never acknowledged dies with this handle, so hand those payloads back to the
// loop-thread carry list. Without this, a write into a dead socket is reported
// as a success and the message is lost with no error anywhere.
//
// Inbound handles are skipped: their "port" is the peer's ephemeral source port,
// so re-queueing would dial a port that belongs to no listener.
//
// This lives outside FreeConnection because that method is only reached while
// the handle is still in conn_map_, and a handle that was already evicted (the
// common case after a health sweep) would otherwise never release its payloads.
static void RequeueUnackedPacketsTo(ex_uv_tcp_t* ex_uv_tcp,
        std::vector<std::shared_ptr<ClientItem>>* carry_list) {
    if (ex_uv_tcp == nullptr || carry_list == nullptr || ex_uv_tcp->is_inbound) {
        return;
    }

    auto packets = std::move(ex_uv_tcp->unacked_packets);
    ex_uv_tcp->unacked_packets.clear();
    ex_uv_tcp->unacked_bytes = 0;

    for (auto& packet : packets) {
        if (packet.payload.empty() || ex_uv_tcp->port == 0) {
            continue;
        }

        auto retry = std::make_shared<ClientItem>();
        retry->des_ip  = std::string(ex_uv_tcp->ip);
        retry->port    = ex_uv_tcp->port;
        retry->msg     = std::move(packet.payload);
        retry->hash64  = packet.hash64;
        retry->type    = packet.msg_type;
        retry->is_retry = true;
        // Continue the count instead of resetting: a peer that accepts the
        // connection but never ACKs would otherwise loop here forever.
        retry->retry_count = packet.retry_count + 1;
        if (retry->retry_count > kMaxSendRetryTimes) {
            SHARDORA_ERROR("[TCP_RECONN] drop msg after %u retries to %s:%d hash64=%lu "
                "— never ACKed", packet.retry_count, ex_uv_tcp->ip, ex_uv_tcp->port, packet.hash64);
            continue;
        }

        carry_list->emplace_back(std::move(retry));
        SHARDORA_WARN("[TCP_RECONN] unacked msg to %s:%d hash64=%lu was never ACKed — re-queued",
            ex_uv_tcp->ip, ex_uv_tcp->port, packet.hash64);
    }
}

// Idempotent: several paths can free the same handle (an on_read error and a late
// on_write failure, or a health sweep racing a write callback). The second call
// would push the same pointer into invalid_conns_ again and uv_close() it twice.
void TcpTransport::FreeConnection(ex_uv_tcp_t* ex_uv_tcp) {
    if (ex_uv_tcp->retired) {
        return;
    }

    std::string peer_spec = std::string(ex_uv_tcp->ip) + ":" + std::to_string(ex_uv_tcp->port);
    auto iter = conn_map_.find(peer_spec);
    if (iter != conn_map_.end() && iter->second == ex_uv_tcp) {
        SHARDORA_DEBUG("[TCP_RECONN] FreeConnection: %s:%d %p — removed from conn_map, "
            "next send will create new connection (closing=%d, type=%d)",
            ex_uv_tcp->ip, ex_uv_tcp->port, static_cast<void*>(&ex_uv_tcp->uv_tcp),
            uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp),
            (int)ex_uv_tcp->uv_tcp.type);
        if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
            uv_read_stop((uv_stream_t*)&ex_uv_tcp->uv_tcp);
        }
        ex_uv_tcp->timeout = common::TimeUtils::TimestampSeconds();
        invalid_conns_.push(ex_uv_tcp);
        conn_map_.erase(iter);
    } else {
        // Not the map's current connection for this peer — either already evicted
        // or replaced by a newer handle. Reclaim this handle only; deleting the
        // map entry here would tear down the live connection instead.
        SHARDORA_DEBUG("[TCP_RECONN] FreeConnection: %s:%d %p is not the current connection "
            "(closing=%d, type=%d) — reclaiming handle only",
            ex_uv_tcp->ip, ex_uv_tcp->port, static_cast<void*>(&ex_uv_tcp->uv_tcp),
            uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp),
            (int)ex_uv_tcp->uv_tcp.type);
        if (!uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp)) {
            uv_read_stop((uv_stream_t*)&ex_uv_tcp->uv_tcp);
        }
        ex_uv_tcp->timeout = common::TimeUtils::TimestampSeconds();
        invalid_conns_.push(ex_uv_tcp);
    }

    // Set before the drain so any uv_write callback that lands after this point
    // routes its payload to the orphan list instead of appending to a list that
    // has just been emptied and will never be looked at again.
    ex_uv_tcp->retired = true;

    // Unconditional: a handle that is no longer in conn_map_ still holds payloads
    // that were never acknowledged, and that is the common case here.
    RequeueUnackedPacketsTo(ex_uv_tcp, &orphaned_unacked_);
}

void TcpTransport::AddConnection(ex_uv_tcp_t* uv_tcp) {
    std::string peer_spec = std::string(uv_tcp->ip) + ":" + std::to_string(uv_tcp->port);
    auto iter = conn_map_.find(peer_spec);
    if (iter != conn_map_.end()) {
        if (iter->second == uv_tcp) {
            return;
        }
        SHARDORA_WARN("[TCP_RECONN] AddConnection: replacing existing connection for %s:%d",
            uv_tcp->ip, uv_tcp->port);
        FreeConnection(iter->second);
    }

    SHARDORA_DEBUG("[TCP_RECONN] AddConnection: %s:%d %p (new connection established)",
        uv_tcp->ip, uv_tcp->port, static_cast<void*>(&uv_tcp->uv_tcp));
    conn_map_[peer_spec] = uv_tcp;
}

std::string TcpTransport::ClearAllConnection() {
    std::string res;
//     std::lock_guard<std::mutex> guard(tcp_transport->send_mutex_);
//     for (auto iter = conn_map_.begin(); iter != conn_map_.end(); ++iter) {
//         if (iter->second == nullptr) {
//             continue;
//         }
// 
//         uv_close((uv_handle_t*)iter->second, on_close);
//     }
// 
//     conn_map_.clear();
    return res;
}

void TcpTransport::SetMessageHash(const transport::protobuf::Header& message) {
    auto tmpHeader = const_cast<transport::protobuf::Header*>(&message);
    std::string hash_str;
    hash_str.reserve(1024);
    hash_str.append(msg_random_);
    uint8_t thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    hash_str.append((char*)&thread_idx, sizeof(thread_idx));
    auto msg_count = ++thread_msg_count_[thread_idx];
    hash_str.append((char*)&msg_count, sizeof(msg_count));
    tmpHeader->set_hash64(common::Hash::Hash64(hash_str));
}

std::string TcpTransport::GetHeaderHashForSign(const transport::protobuf::Header& message) {
    //assert(message.has_hash64());
    //assert(message.hash64() != 0);
    std::string msg_for_hash;
    msg_for_hash.reserve(3 * 1024 * 1024);
    msg_for_hash.append(message.des_dht_key());
    uint64_t hash64 = message.hash64();
    msg_for_hash.append(std::string((char*)&hash64, sizeof(hash64)));
    int32_t sharding_id = message.src_sharding_id();
    msg_for_hash.append(std::string((char*)&sharding_id, sizeof(sharding_id)));
    uint32_t type = message.type();
    msg_for_hash.append(std::string((char*)&type, sizeof(type)));
    int32_t version = message.version();
    msg_for_hash.append(std::string((char*)&version, sizeof(version)));
    return common::Hash::keccak256(msg_for_hash);
}

}  // namespace transport

}  // namespace shardora

#endif