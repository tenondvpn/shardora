#include "init/tx_ws_server.h"

#include <arpa/inet.h>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <ctime>

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#include "common/encode.h"
#include "common/log.h"

namespace shardora {
namespace init {

static const std::string kWsMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// ── destructor ───────────────────────────────────────────────────────────────

TxWsServer::~TxWsServer() {
    if (running_.load()) {
        uv_async_send(&async_);
        if (loop_thread_.joinable()) loop_thread_.join();
        uv_loop_close(loop_);
        delete loop_;
    }
}

// ── Init ─────────────────────────────────────────────────────────────────────

int TxWsServer::Init(const std::string& ip, uint16_t port) {
    loop_ = new uv_loop_t;
    uv_loop_init(loop_);
    loop_->data = this;

    uv_tcp_init(loop_, &server_tcp_);
    server_tcp_.data = this;

    // Set SO_REUSEADDR + SO_REUSEPORT before bind so the port can be reused
    // immediately after a restart (avoids TIME_WAIT / EADDRINUSE).
    uv_os_fd_t fd;
    if (uv_fileno(reinterpret_cast<uv_handle_t*>(&server_tcp_), &fd) == 0) {
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif
    }

    sockaddr_in addr{};
    uv_ip4_addr(ip.c_str(), port, &addr);

    int r = uv_tcp_bind(&server_tcp_, reinterpret_cast<const sockaddr*>(&addr), 0);
    if (r != 0) {
        SHARDORA_ERROR("[TxWsServer] bind failed on %s:%u: %s", ip.c_str(), port, uv_strerror(r));
        return 1;
    }

    r = uv_listen(reinterpret_cast<uv_stream_t*>(&server_tcp_), 128, OnNewConnection);
    if (r != 0) {
        SHARDORA_ERROR("[TxWsServer] listen failed: %s", uv_strerror(r));
        return 1;
    }

    uv_async_init(loop_, &async_, OnAsync);
    async_.data = this;

    running_.store(true);
    loop_thread_ = std::thread(&TxWsServer::RunLoop, this);
    SHARDORA_DEBUG("[TxWsServer] listening on %s:%u (private loop, isolated from TcpTransport)",
              ip.c_str(), port);
    return 0;
}

void TxWsServer::RunLoop() {
    uv_run(loop_, UV_RUN_DEFAULT);
    running_.store(false);
}

// ── OnNewBlock (consensus thread) ────────────────────────────────────────────

void TxWsServer::OnNewBlock(std::shared_ptr<view_block::protobuf::ViewBlockItem> view_block) {
    if (!running_.load(std::memory_order_acquire)) return;
    if (!view_block || view_block->block_info().tx_list_size() == 0) return;
    block_queue_.push(std::move(view_block));
    uv_async_send(&async_);
}

void TxWsServer::OnTxStatusChange(const std::string& tx_hash_hex,
                                   transport::MessageHandleStatus status) {
    if (!running_.load(std::memory_order_acquire)) return;
    if (tx_hash_hex.empty()) return;
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    status_queue_[thread_idx].push({tx_hash_hex, status});
    uv_async_send(&async_);
}

// ── libuv callbacks ───────────────────────────────────────────────────────────

void TxWsServer::OnAsync(uv_async_t* handle) {
    auto* self = static_cast<TxWsServer*>(handle->data);

    // ── drain status-change queue (tx rejected/invalid before block) ─────────
    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        TxStatusItem item;
        while (self->status_queue_[i].pop(&item)) {
            self->CompleteAndPush(item.tx_hash_hex,
                                BuildStatusJson(item.tx_hash_hex, item.status));
        }
    }

    // ── drain block queue ─────────────────────────────────────────────────────
    std::shared_ptr<view_block::protobuf::ViewBlockItem> vb;
    while (self->block_queue_.pop(&vb)) {
        const auto& block = vb->block_info();
        for (int i = 0; i < block.tx_list_size(); ++i) {
            const auto& tx = block.tx_list(i);
            if (!tx.has_tx_hash() || tx.tx_hash().empty()) continue;
            std::string hex = common::Encode::HexEncode(tx.tx_hash());
            self->CompleteAndPush(hex, BuildTxJson(*vb, tx));
        }
    }

    // ── evict expired cache entries ───────────────────────────────────────────
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    for (auto it = self->completed_txs_.begin(); it != self->completed_txs_.end(); ) {
        if (it->second.expire_sec <= now)
            it = self->completed_txs_.erase(it);
        else
            ++it;
    }
}

void TxWsServer::OnNewConnection(uv_stream_t* srv, int status) {
    if (status < 0) {
        SHARDORA_ERROR("[TxWsServer] accept error: %s", uv_strerror(status));
        return;
    }
    auto* self = static_cast<TxWsServer*>(srv->data);

    auto* c = new Conn{};
    c->server = self;
    uv_tcp_init(self->loop_, &c->tcp);
    c->tcp.data = c;

    if (uv_accept(srv, reinterpret_cast<uv_stream_t*>(&c->tcp)) == 0) {
        {
            std::lock_guard<std::mutex> lk(self->mutex_);
            self->conn_to_hashes_[c];   // register with empty set
        }
        uv_read_start(reinterpret_cast<uv_stream_t*>(&c->tcp), OnAlloc, OnRead);
    } else {
        delete c;
    }
}

void TxWsServer::OnAlloc(uv_handle_t* /*h*/, size_t suggested, uv_buf_t* buf) {
    buf->base = new char[suggested];
    buf->len  = suggested;
}

void TxWsServer::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    auto* c = static_cast<Conn*>(stream->data);
    if (nread > 0) {
        c->server->HandleRawData(c, buf->base, nread);
    } else if (nread < 0) {
        c->server->CloseConn(c);
    }
    delete[] buf->base;
}

void TxWsServer::OnWrite(uv_write_t* req, int /*status*/) {
    auto* wr = reinterpret_cast<WriteReq*>(req);
    Conn* c = wr->conn;
    delete[] wr->buf;
    delete wr;

    c->write_pending = false;
    c->server->FlushConn(c);
}

void TxWsServer::OnClose(uv_handle_t* handle) {
    // Conn was already deleted in CloseConn; handle->data is null there.
    (void)handle;
}

// ── HTTP Upgrade ──────────────────────────────────────────────────────────────

void TxWsServer::HandleRawData(Conn* c, const char* data, ssize_t len) {
    c->read_buf.append(data, len);
    if (!c->upgraded) {
        TryHttpUpgrade(c);
        return;
    }
    // Decode all complete WebSocket frames from read_buf.
    while (true) {
        const auto& buf = c->read_buf;
        if (buf.size() < 2) break;

        uint8_t b0 = static_cast<uint8_t>(buf[0]);
        uint8_t b1 = static_cast<uint8_t>(buf[1]);
        bool    masked      = (b1 & 0x80) != 0;
        uint64_t payload_len = b1 & 0x7f;
        size_t   header_len  = 2;

        if (payload_len == 126) {
            if (buf.size() < 4) break;
            payload_len = (static_cast<uint8_t>(buf[2]) << 8) |
                           static_cast<uint8_t>(buf[3]);
            header_len = 4;
        } else if (payload_len == 127) {
            if (buf.size() < 10) break;
            payload_len = 0;
            for (int i = 0; i < 8; ++i)
                payload_len = (payload_len << 8) | static_cast<uint8_t>(buf[2 + i]);
            header_len = 10;
        }

        size_t mask_len = masked ? 4 : 0;
        size_t total    = header_len + mask_len + static_cast<size_t>(payload_len);
        if (buf.size() < total) break;

        uint8_t opcode = b0 & 0x0f;
        if (opcode == 0x8) { CloseConn(c); return; }  // close frame

        if (opcode == 0x1 || opcode == 0x2) {
            std::string payload(buf.data() + header_len + mask_len,
                                static_cast<size_t>(payload_len));
            if (masked) {
                const char* mask = buf.data() + header_len;
                for (size_t i = 0; i < static_cast<size_t>(payload_len); ++i)
                    payload[i] ^= mask[i % 4];
            }
            HandleWsFrame(c, payload);
        }
        c->read_buf.erase(0, total);
    }
}

bool TxWsServer::TryHttpUpgrade(Conn* c) {
    auto pos = c->read_buf.find("\r\n\r\n");
    if (pos == std::string::npos) return false;

    std::string headers = c->read_buf.substr(0, pos + 4);
    c->read_buf.erase(0, pos + 4);

    auto kpos = headers.find("Sec-WebSocket-Key:");
    if (kpos == std::string::npos) { CloseConn(c); return false; }
    kpos += 18;
    while (kpos < headers.size() && headers[kpos] == ' ') ++kpos;
    auto epos = headers.find("\r\n", kpos);
    if (epos == std::string::npos) { CloseConn(c); return false; }
    std::string key = headers.substr(kpos, epos - kpos);

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + WsAcceptKey(key) + "\r\n\r\n";

    c->upgraded = true;

    auto* wr  = new WriteReq{};
    wr->conn  = c;
    wr->buf   = new char[response.size()];
    memcpy(wr->buf, response.data(), response.size());
    uv_buf_t wbuf = uv_buf_init(wr->buf, static_cast<unsigned>(response.size()));
    uv_write(&wr->req, reinterpret_cast<uv_stream_t*>(&c->tcp), &wbuf, 1, OnWrite);
    return true;
}

// ── WebSocket frame handling ──────────────────────────────────────────────────

void TxWsServer::CompleteAndPush(const std::string& hash_hex, const std::string& json) {
    // Cache the result for late subscribers.
    int64_t expire = static_cast<int64_t>(std::time(nullptr)) + kCompletedTxTtlSec;
    std::string frame = MakeTextFrame(json);
    completed_txs_[hash_hex] = {frame, expire};

    // Push to any current subscribers.
    std::unordered_set<Conn*> conns;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = hash_to_conns_.find(hash_hex);
        if (it != hash_to_conns_.end()) conns = it->second;
    }
    for (Conn* c : conns) {
        if (conn_to_hashes_.count(c) == 0) continue;
        EnqueueFrame(c, frame);
    }
    if (!conns.empty()) {
        SHARDORA_DEBUG("[TxWsServer] pushed result for tx %s to %zu client(s)",
                  hash_hex.c_str(), conns.size());
    }
}

std::string TxWsServer::BuildStatusJson(const std::string& tx_hash_hex,
                                         transport::MessageHandleStatus status) {
    std::ostringstream o;
    o << "{\"tx_hash\":\"" << tx_hash_hex << "\","
      << "\"status\":"     << static_cast<int32_t>(status) << ","
      << "\"msg\":\""      << transport::MessageStatusToString(status) << "\"}";
    return o.str();
}

void TxWsServer::HandleWsFrame(Conn* c, const std::string& payload) {
    static const std::string kSub   = "subscribe:";
    static const std::string kUnsub = "unsubscribe:";

    if (payload.rfind(kSub, 0) == 0) {
        std::string hash = payload.substr(kSub.size());
        if (hash.empty()) { EnqueueFrame(c, R"({"error":"empty txhash"})"); return; }

        // Check cache first: if this tx already completed, push immediately.
        auto cached = completed_txs_.find(hash);
        if (cached != completed_txs_.end()) {
            SHARDORA_DEBUG("[TxWsServer] late-subscribe hit cache for tx %s", hash.c_str());
            EnqueueFrame(c, cached->second.frame);
            return;
        }

        {
            std::lock_guard<std::mutex> lk(mutex_);
            hash_to_conns_[hash].insert(c);
            conn_to_hashes_[c].insert(hash);
            c->subscriptions.insert(hash);
        }
        SHARDORA_DEBUG("[TxWsServer] subscribed txhash: %s", hash.c_str());
        EnqueueFrame(c, R"({"status":"subscribed","txhash":")" + hash + R"("})");

    } else if (payload.rfind(kUnsub, 0) == 0) {
        std::string hash = payload.substr(kUnsub.size());
        {
            std::lock_guard<std::mutex> lk(mutex_);
            auto hit = hash_to_conns_.find(hash);
            if (hit != hash_to_conns_.end()) {
                hit->second.erase(c);
                if (hit->second.empty()) hash_to_conns_.erase(hit);
            }
            conn_to_hashes_[c].erase(hash);
            c->subscriptions.erase(hash);
        }
        SHARDORA_DEBUG("[TxWsServer] unsubscribed txhash: %s", hash.c_str());
        EnqueueFrame(c, R"({"status":"unsubscribed","txhash":")" + hash + R"("})");

    } else {
        EnqueueFrame(c, R"({"error":"unknown command"})");
    }
}

void TxWsServer::CloseConn(Conn* c) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = conn_to_hashes_.find(c);
        if (it == conn_to_hashes_.end()) return;  // already cleaned
        for (const auto& h : it->second) {
            auto hit = hash_to_conns_.find(h);
            if (hit != hash_to_conns_.end()) {
                hit->second.erase(c);
                if (hit->second.empty()) hash_to_conns_.erase(hit);
            }
        }
        conn_to_hashes_.erase(it);
    }
    c->tcp.data = nullptr;
    uv_close(reinterpret_cast<uv_handle_t*>(&c->tcp),
             [](uv_handle_t* h) { delete static_cast<Conn*>(h->data); });
    SHARDORA_DEBUG("[TxWsServer] connection closed");
}

// ── Send helpers ──────────────────────────────────────────────────────────────

void TxWsServer::EnqueueFrame(Conn* c, const std::string& json) {
    c->send_queue.push_back(MakeTextFrame(json));
    if (!c->write_pending) FlushConn(c);
}

void TxWsServer::FlushConn(Conn* c) {
    if (c->send_queue.empty() || c->write_pending) return;
    std::string frame = std::move(c->send_queue.front());
    c->send_queue.erase(c->send_queue.begin());

    c->write_pending = true;
    auto* wr  = new WriteReq{};
    wr->conn  = c;
    wr->buf   = new char[frame.size()];
    memcpy(wr->buf, frame.data(), frame.size());
    uv_buf_t wbuf = uv_buf_init(wr->buf, static_cast<unsigned>(frame.size()));
    uv_write(&wr->req, reinterpret_cast<uv_stream_t*>(&c->tcp), &wbuf, 1, OnWrite);
}

// ── WebSocket frame builder ───────────────────────────────────────────────────

std::string TxWsServer::MakeTextFrame(const std::string& payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));  // FIN + text opcode
    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 65535) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((len >> 8) & 0xff));
        frame.push_back(static_cast<char>(len & 0xff));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<char>((len >> (8 * i)) & 0xff));
    }
    frame.append(payload);
    return frame;
}

// ── WebSocket handshake ───────────────────────────────────────────────────────

std::string TxWsServer::WsAcceptKey(const std::string& client_key) {
    std::string combined = client_key + kWsMagic;
    unsigned char sha1[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.data()),
         combined.size(), sha1);
    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, sha1, SHA_DIGEST_LENGTH);
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

// ── JSON builder ──────────────────────────────────────────────────────────────

std::string TxWsServer::BuildTxJson(
        const view_block::protobuf::ViewBlockItem& vb,
        const block::protobuf::BlockTx& tx) {
    const auto& block = vb.block_info();
    const auto& qc    = vb.qc();
    std::ostringstream o;
    o << "{"
      << "\"tx_hash\":\""    << common::Encode::HexEncode(tx.tx_hash()) << "\","
      << "\"from\":\""       << common::Encode::HexEncode(tx.from())    << "\","
      << "\"to\":\""         << common::Encode::HexEncode(tx.to())      << "\","
      << "\"amount\":"       << tx.amount()                             << ","
      << "\"gas_used\":"     << tx.gas_used()                           << ","
      << "\"gas_price\":"    << tx.gas_price()                          << ","
      << "\"status\":"       << tx.status()                             << ","
      << "\"step\":"         << static_cast<int>(tx.step())             << ","
      << "\"nonce\":"        << tx.nonce()                              << ","
      << "\"block_height\":" << block.height()                          << ","
      << "\"chainid\":" << block.chain_id()                             << ","
      << "\"network_id\":"   << qc.network_id()                         << ","
      << "\"pool_index\":"   << qc.pool_index()                         << ","
      << "\"timestamp\":"    << block.timestamp();

    if (tx.has_contract_input() && !tx.contract_input().empty())
        o << ",\"contract_input\":\"" << common::Encode::HexEncode(tx.contract_input()) << "\"";

    if (tx.has_output() && !tx.output().empty())
        o << ",\"output\":\"" << common::Encode::HexEncode(tx.output()) << "\"";

    if (tx.events_size() > 0) {
        o << ",\"events\":[";
        for (int i = 0; i < tx.events_size(); ++i) {
            if (i) o << ",";
            const auto& ev = tx.events(i);
            o << "{\"data\":\"" << common::Encode::HexEncode(ev.data()) << "\","
              << "\"topics\":[";
            for (int j = 0; j < ev.topics_size(); ++j) {
                if (j) o << ",";
                o << "\"" << common::Encode::HexEncode(ev.topics(j)) << "\"";
            }
            o << "]}";
        }
        o << "]";
    }
    o << "}";
    return o.str();
}

}  // namespace init
}  // namespace shardora
