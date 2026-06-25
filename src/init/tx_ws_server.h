#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <uv.h>

#include "common/thread_safe_queue.h"
#include "common/utils.h"
#include "protos/view_block.pb.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace init {

// TxWsServer: lightweight WebSocket server built on libuv.
//
// Uses a private uv_loop (NOT uv_default_loop) running in a dedicated thread,
// so it is completely isolated from TcpTransport which owns uv_default_loop.
//
// Wire protocol (client -> server, text frame payload):
//   subscribe:<txhash_hex>
//   unsubscribe:<txhash_hex>
//
// Server -> client push: JSON text frame (see BuildTxJson).
//
// Late-subscribe support:
//   Completed tx results (both block confirmations and error statuses) are
//   cached for kCompletedTxTtlSec seconds.  When a client subscribes to a
//   hash that is already in the cache the result is pushed immediately.
class TxWsServer {
public:
    TxWsServer() = default;
    ~TxWsServer();

    // Start listening on ip:port in a dedicated background thread.
    int Init(const std::string& ip, uint16_t port);

    // Called from any thread when a tx reaches a terminal error status
    // (anything other than kMessageHandle / kTxAccept).
    // Pushes a JSON error frame to all subscribers of tx_hash_hex.
    void OnTxStatusChange(const std::string& tx_hash_hex,
                          transport::MessageHandleStatus status);

    // Called from any thread when a new block is committed.
    // Enqueues the block; the libuv loop thread drains the queue and pushes
    // JSON frames to matching subscribers.
    void OnNewBlock(std::shared_ptr<view_block::protobuf::ViewBlockItem> view_block);

private:
    // How long (seconds) to keep completed tx results for late subscribers.
    static constexpr int64_t kCompletedTxTtlSec = 30;

    // ── per-connection state ──────────────────────────────────────────────
    struct Conn {
        uv_tcp_t    tcp;            // must be first
        TxWsServer* server  = nullptr;
        bool        upgraded = false;
        std::string read_buf;
        std::unordered_set<std::string> subscriptions;
        std::vector<std::string> send_queue;
        bool write_pending = false;
    };

    // ── per-write request (owns its buffer, avoids touching handle->data) ─
    struct WriteReq {
        uv_write_t req;     // must be first
        Conn*      conn = nullptr;
        char*      buf  = nullptr;
    };

    // ── completed tx cache entry ──────────────────────────────────────────
    struct CompletedTx {
        std::string frame;      // ready-to-send WebSocket text frame
        int64_t     expire_sec; // unix timestamp after which entry is evicted
    };

    // ── libuv callbacks ───────────────────────────────────────────────────
    static void OnNewConnection(uv_stream_t* srv, int status);
    static void OnAlloc(uv_handle_t* handle, size_t suggested, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWrite(uv_write_t* req, int status);
    static void OnClose(uv_handle_t* handle);
    // Wakes the loop from OnNewBlock / OnTxStatusChange (cross-thread).
    static void OnAsync(uv_async_t* handle);

    // ── internal helpers ──────────────────────────────────────────────────
    void RunLoop();
    void HandleRawData(Conn* c, const char* data, ssize_t len);
    bool TryHttpUpgrade(Conn* c);
    void HandleWsFrame(Conn* c, const std::string& payload);
    void CloseConn(Conn* c);
    void EnqueueFrame(Conn* c, const std::string& json);
    void FlushConn(Conn* c);

    // Cache a completed tx frame and push to any current subscribers.
    // Called only from the libuv loop thread.
    void CompleteAndPush(const std::string& hash_hex, const std::string& json);

    static std::string MakeTextFrame(const std::string& payload);
    static std::string BuildTxJson(
        const view_block::protobuf::ViewBlockItem& vb,
        const block::protobuf::BlockTx& tx);
    static std::string BuildStatusJson(
        const std::string& tx_hash_hex,
        transport::MessageHandleStatus status);
    static std::string WsAcceptKey(const std::string& client_key);

    // ── state ─────────────────────────────────────────────────────────────
    uv_loop_t*  loop_  = nullptr;   // private loop, NOT uv_default_loop
    uv_tcp_t    server_tcp_{};
    uv_async_t  async_{};
    std::thread loop_thread_;
    std::atomic<bool> running_{false};

    // Cross-thread block queue: OnNewBlock (any thread) pushes here;
    // OnAsync (libuv loop thread) drains and broadcasts.
    common::ThreadSafeQueue<
        std::shared_ptr<view_block::protobuf::ViewBlockItem>, 4096> block_queue_;

    // Cross-thread status-change queue: OnTxStatusChange (any thread) pushes here;
    // OnAsync (libuv loop thread) drains and pushes error frames to subscribers.
    struct TxStatusItem {
        std::string tx_hash_hex;
        transport::MessageHandleStatus status;
    };
    common::ThreadSafeQueue<TxStatusItem, 4096> status_queue_[common::kMaxThreadCount];

    // Subscription maps — accessed only from the libuv loop thread, no lock needed.
    std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_set<Conn*>> hash_to_conns_;
    std::unordered_map<Conn*, std::unordered_set<std::string>> conn_to_hashes_;

    // Completed tx cache — loop thread only, no lock needed.
    // Stores the ready-to-send frame for recently completed txs so that
    // late subscribers receive the result immediately on subscribe.
    std::unordered_map<std::string, CompletedTx> completed_txs_;

    DISALLOW_COPY_AND_ASSIGN(TxWsServer);
};

}  // namespace init
}  // namespace shardora
