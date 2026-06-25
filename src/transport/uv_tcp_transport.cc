#include "transport/uv_tcp_transport.h"
#ifdef SHARDORA_USE_UV

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
static uv_loop_t* loop;
TcpTransport* tcp_transport = nullptr;
uv_tcp_t* socket;
uv_os_sock_t sock;
static uv_async_t async_handle;
std::atomic<bool> uv_transport_inited = false;

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
};

void on_close(uv_handle_t* handle) {
    SHARDORA_INFO("close called: %p!", static_cast<void*>(handle));
    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)handle;
    //assert(ex_uv_tcp->msg_decoder != nullptr);
    if (ex_uv_tcp->msg_decoder) {
        delete ex_uv_tcp->msg_decoder;
        ex_uv_tcp->msg_decoder = nullptr;
    }

    free(ex_uv_tcp);
}

void on_write(uv_write_t* req, int status) {
    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)req->handle;
    if (status < 0) {
        // Write failed (broken pipe, connection reset, etc.) — remove the dead
        // connection from conn_map_ so the next Send() creates a fresh one.
        SHARDORA_WARN("[TCP_RECONN] on_write failed: %s:%d, status=%d (%s) — freeing connection",
            ex_uv_tcp->ip, ex_uv_tcp->port, status, uv_strerror(status));
        tcp_transport->FreeConnection(ex_uv_tcp);
        // Note: Do NOT call uv_close here. FreeConnection puts the handle in invalid_conns_
        // queue, and RealFreeInvalidConnections will close it later with proper timing.
    } else {
        SHARDORA_DEBUG("[TCP_RECONN] on_write success: %s:%d", ex_uv_tcp->ip, ex_uv_tcp->port);
    }
    free(req);
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
    if (status < 0) {
        SHARDORA_WARN("[TCP_RECONN] failed to connect %s:%d, status=%d (%s) — will retry on next send",
            ex_uv_tcp->ip, ex_uv_tcp->port, status, uv_strerror(status));
        connect_ex_t* ex_conn = (connect_ex_t*)connection;
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
    
    // Enable TCP keepalive: detect dead peers within ~120s (increased from 60s)
    // Longer interval reduces false disconnections during network stress
    uv_tcp_keepalive(&ex_uv_tcp->uv_tcp, 1, 120);
    // Disable Nagle's algorithm for lower latency
    uv_tcp_nodelay(&ex_uv_tcp->uv_tcp, 1);

    uv_write_t *req = (uv_write_t*)malloc(sizeof(uv_write_t));
    connect_ex_t* ex_conn = (connect_ex_t*)connection;
    uv_buf_t uv_buf = uv_buf_init((char*)ex_conn->msg->c_str(), ex_conn->msg->size());
    
    // 应用层网络延迟注入 - 仅在启用时应用
    // 在高并发压测下，延迟注入可能导致包头破坏，建议禁用
    bool network_enabled = tcp_transport->GetNetworkDelaySimulator().IsEnabled();
    if (network_enabled) {
        if (tcp_transport->GetNetworkDelaySimulator().ShouldDropPacket()) {
            SHARDORA_DEBUG("[NETWORK_SIM] dropping packet on connect to %s:%d",
                ex_uv_tcp->ip, ex_uv_tcp->port);
            free(req);
            delete ex_conn->msg;
            free(ex_conn);
            uv_close((uv_handle_t*)&ex_uv_tcp->uv_tcp, on_close);
            return;
        }
        // Apply delay after connection is established but before sending
        tcp_transport->GetNetworkDelaySimulator().ApplyDelay();
    }
    
    uv_write(req, (uv_stream_t*)&ex_uv_tcp->uv_tcp, &uv_buf, 1, on_write);
    delete ex_conn->msg;
    free(ex_conn);
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

    ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)malloc(sizeof(ex_uv_tcp_t));
    memset(ex_uv_tcp, 0, sizeof(ex_uv_tcp_t));
    uv_tcp_init(loop, &ex_uv_tcp->uv_tcp);
    ex_uv_tcp->uv_tcp.data = ex_uv_tcp;
    ex_uv_tcp->msg_decoder = new MsgDecoder();
    if (uv_accept(server, (uv_stream_t*)&ex_uv_tcp->uv_tcp) == 0) {
        int new_recv_size = kTcpBufferSize;
        uv_recv_buffer_size((uv_handle_t *)&ex_uv_tcp->uv_tcp, &new_recv_size);
        int new_send_size = kTcpBufferSize;
        uv_send_buffer_size((uv_handle_t *)&ex_uv_tcp->uv_tcp, &new_send_size);
        // Enable TCP keepalive: detect dead peers within ~30s
        uv_tcp_keepalive(&ex_uv_tcp->uv_tcp, 1, 30);
        
        struct sockaddr_storage peername;
        int namelen = sizeof(peername);
        uv_tcp_getpeername(&ex_uv_tcp->uv_tcp, (struct sockaddr*)&peername, &namelen);
        struct sockaddr_in* addr = (struct sockaddr_in*)&peername;
        uv_inet_ntop(AF_INET, &addr->sin_addr, ex_uv_tcp->ip, sizeof(ex_uv_tcp->ip));
        ex_uv_tcp->port = ntohs(addr->sin_port);
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
                free(tmp);
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
                        free(tmp);
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
    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        MessagePtr msg_ptr;
        while (local_messages_[i].pop(&msg_ptr)) {
            msg_handler_->HandleMessage(msg_ptr);
        }

        while (true) {
            std::shared_ptr<ClientItem> item_ptr = nullptr;
            output_queues_[i].pop(&item_ptr);
            if (item_ptr == nullptr) {
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
                    // Check if connection is still valid:
                    // 1. Not closing (uv_is_closing returns true if close was called)
                    // 2. Still active (has pending I/O operations or is readable)
                    // 3. Handle type is still UV_TCP (not corrupted)
                    // 4. Not in invalid_conns_ queue (being cleaned up)
                    if (uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp) ||
                        ex_uv_tcp->uv_tcp.type != UV_TCP) {
                        SHARDORA_WARN("[TCP_RECONN] stale connection detected: %s:%d (closing=%d, type=%d) — reconnecting",
                            des_ip.c_str(), des_port, 
                            uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp),
                            (int)ex_uv_tcp->uv_tcp.type);
                        transport::TcpTransport::Instance()->FreeConnection(ex_uv_tcp);
                        ex_uv_tcp = nullptr;
                    } else {
                        // Connection looks good, reuse it
                        SHARDORA_DEBUG("[TCP_RECONN] reusing existing connection: %s:%d %p",
                            des_ip.c_str(), des_port, static_cast<void*>(&ex_uv_tcp->uv_tcp));
                    }
                }
            }

            if (ex_uv_tcp == nullptr) {
                ex_uv_tcp_t* ex_uv_tcp = (ex_uv_tcp_t*)malloc(sizeof(ex_uv_tcp_t));
                memset(ex_uv_tcp, 0, sizeof(ex_uv_tcp_t));
                uv_tcp_init(loop, &ex_uv_tcp->uv_tcp);
                struct sockaddr_in server_addr;
                uv_ip4_addr(des_ip.c_str(), des_port, &server_addr);
                connect_ex_t* ex_conn = (connect_ex_t*)malloc(sizeof(connect_ex_t));
                std::string* msg = new std::string();
                PacketHeader header(item_ptr->msg.size(), 0);
                msg->append((char*)&header, sizeof(header));
                msg->append(item_ptr->msg);
                ex_conn->msg = msg;
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
                    free(ex_uv_tcp);
                    free(ex_conn);
                } else {
                    SHARDORA_DEBUG("[TCP_RECONN] initiated connect to %s:%d, hash64=%lu", 
                        des_ip.c_str(), des_port, item_ptr->hash64);
                }
            } else {
                std::string tmp_msg;
                PacketHeader header(item_ptr->msg.size(), 0);
                tmp_msg.append((char*)&header, sizeof(header));
                tmp_msg.append(item_ptr->msg);
                uv_buf_t buf = uv_buf_init((char*)tmp_msg.c_str(), tmp_msg.size());
                uv_write_t *req = (uv_write_t*)malloc(sizeof(uv_write_t));
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
                        free(req);
                        continue;
                    }
                    transport::TcpTransport::Instance()->GetNetworkDelaySimulator().ApplyDelay();
                }
                
                uv_write(req, (uv_stream_t*)&ex_uv_tcp->uv_tcp, &buf, 1, on_write);
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

    uv_ip4_addr(splits[0], port, &addr);
    int bind_res = uv_tcp_bind(&server, (const struct sockaddr*)&addr, UV_TCP_REUSEPORT);
    if (bind_res < 0) {
        SHARDORA_ERROR("bind failed: %s", uv_strerror(bind_res));
    }

    // 2. 深度注入：通过底层 fd 设置 SO_REUSEADDR (防止 TIME_WAIT 导致绑定失败)
    uv_os_fd_t fd;
    if (uv_fileno((const uv_handle_t*)&server, &fd) == 0) {
        int opt = 1;
        // 在 Linux 系统下，显式设置 SO_REUSEADDR
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    // uv_tcp_bind(&server, (const struct sockaddr*)&addr, UV_UDP_REUSEADDR);
    int32_t try_times = 0;
    do {
        int r = uv_listen((uv_stream_t*)&server, 128, on_new_connection);
        if (r == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100000ull));
            if (uv_is_active((uv_handle_t*)&server)) {
                break;
            }
        
            SHARDORA_FATAL("listen failed: %s: %d, server inactive.", splits[0], port);
            return;
        }
        
        SHARDORA_ERROR("listen failed: %s: %d, res: %d", splits[0], port, r);
        std::this_thread::sleep_for(std::chrono::microseconds(100000ull));
    } while (try_times++ < 10);

    if (try_times >= 10) {
        SHARDORA_FATAL("listen failed: %s: %d", splits[0], port);
        return;
    }
    
    uv_signal_t sig;
    uv_signal_init(loop, &sig);
    uv_signal_start(&sig, signal_handler, SIGINT);
    SHARDORA_DEBUG("init uv tcp transport success: %s", ip_port_.c_str());
    uv_async_init(loop, &async_handle, uv_async_cb);
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

void TcpTransport::FreeConnection(ex_uv_tcp_t* ex_uv_tcp) {
    std::string peer_spec = std::string(ex_uv_tcp->ip) + ":" + std::to_string(ex_uv_tcp->port);
    auto iter = conn_map_.find(peer_spec);
    if (iter != conn_map_.end()) {
        SHARDORA_WARN("[TCP_RECONN] FreeConnection: %s:%d %p — removed from conn_map, "
            "next send will create new connection (closing=%d, type=%d)",
            ex_uv_tcp->ip, ex_uv_tcp->port, static_cast<void*>(&ex_uv_tcp->uv_tcp),
            uv_is_closing((uv_handle_t*)&ex_uv_tcp->uv_tcp),
            (int)ex_uv_tcp->uv_tcp.type);
        ex_uv_tcp->timeout = common::TimeUtils::TimestampSeconds();
        invalid_conns_.push(ex_uv_tcp);
        conn_map_.erase(iter);
    }
}

void TcpTransport::AddConnection(ex_uv_tcp_t* uv_tcp) {
    std::string peer_spec = std::string(uv_tcp->ip) + ":" + std::to_string(uv_tcp->port);
    auto iter = conn_map_.find(peer_spec);
    if (iter != conn_map_.end()) {
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