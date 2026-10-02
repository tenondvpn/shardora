#pragma once

#ifdef SHARDORA_USE_UV
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <uv.h>

#include "common/random.h"
#include "common/thread_safe_queue.h"
#include "protos/transport.pb.h"
#include "transport/msg_encoder.h"
#include "transport/msg_decoder.h"
#include "transport/multi_thread.h"
#include "transport/transport_utils.h"
#include "transport/network_delay_simulator.h"

namespace shardora {

namespace transport {

// A packet that uv_write() has accepted into the kernel send buffer but that
// has not yet been acknowledged by the peer. on_write reports status==0 once
// the bytes reach the local socket buffer, which says nothing about delivery:
// if the peer turns out to be a half-open zombie the kernel discards the
// buffer (TCP_USER_TIMEOUT) with no further callback. Keeping the payload here
// lets the connection teardown path re-queue it instead of losing it silently.
struct UnackedPacket {
    std::string payload;
    uint64_t hash64 = 0;
    uint32_t msg_type = 0;
    // Retry budget consumed so far. Re-queueing after a teardown continues from
    // here rather than resetting, so a peer that accepts connections but never
    // ACKs still reaches the drop cap instead of looping forever.
    uint32_t retry_count = 0;
    // TimestampMs when the write was accepted. Pruning only releases entries that
    // have had time to reach the wire: tcpi_unacked counts packets the kernel has
    // transmitted and not yet had ACKed, so data still sitting in the send queue
    // because the peer's receive window is closed is not counted at all and would
    // otherwise be cleared as if it had been acknowledged.
    uint64_t enqueued_ms = 0;
};

struct ex_uv_tcp_t {
    uv_tcp_t uv_tcp;
    MsgDecoder* msg_decoder;
    char ip[64];
    uint16_t port;
    uint64_t timeout;
    uint64_t last_recv_ts;  // epoch seconds of last successful on_read; 0 = never received
    uint64_t connect_ts;    // epoch seconds when connection became usable
    // Set only for handles accepted from the listener, whose "port" is the
    // peer's ephemeral source port rather than a port we can dial back.
    bool is_inbound = false;
    // Set by FreeConnection once the handle has left conn_map_. A uv_write issued
    // before the teardown can still have its callback pending, and that callback
    // would otherwise record the payload on a handle nothing drains any more.
    bool retired = false;
    // Loop thread only. Bounded by the kMaxUnacked* constants in the .cc; oldest
    // first.
    std::vector<UnackedPacket> unacked_packets;
    uint64_t unacked_bytes = 0;
};

class TcpTransport {
public:
    static TcpTransport* Instance();
    int Init(
        const std::string& ip_port, 
        int backlog, 
        bool create_server, 
        MultiThreadHandler* msg_handler);
    int Start(bool hold);
    void Stop();
    int Send(
        const std::string& ip,
        uint16_t port,
        transport::protobuf::Header& message);
    int Send(
        const std::string& ip,
        uint16_t port,
        uint32_t type,
        uint64_t hash64,
        std::string&& serialized_msg);
    int Send(
        std::shared_ptr<tnet::TcpInterface> conn,
        const transport::protobuf::Header& message);
    int Send(
        std::shared_ptr<tnet::TcpInterface> conn,
        const std::string& message);
    int SendToLocal(transport::protobuf::Header& message);
    int GetSocket();
    void SetMessageHash(const transport::protobuf::Header& message);
    std::string GetHeaderHashForSign(const transport::protobuf::Header& message);
    void AddConnection(ex_uv_tcp_t* uv_tcp);
    ex_uv_tcp_t* GetConnection(const std::string& ip, uint16_t port);
    void FreeConnection(ex_uv_tcp_t* uv_tcp);
    std::string ClearAllConnection();

    MultiThreadHandler* msg_handler();

    void RealFreeInvalidConnections();
    void CheckConnectionsHealth();
    void AddLocalMessage(transport::MessagePtr msg_ptr);
    uint8_t GetThreadIndexWithPool(uint32_t pool_index);
    
    // 获取网络延迟模拟器 (用于应用层延迟注入)
    NetworkDelaySimulator& GetNetworkDelaySimulator() {
        return network_delay_simulator_;
    }

private:
    TcpTransport();
    ~TcpTransport();
    void Run();
    void Output();

    // [TCP_RECONN] Reduced from 180s to 10s. Dead connections should be cleaned up
    // quickly so their handles don't accumulate. 10s is enough grace period for
    // in-flight uv_write callbacks to complete.
    static const uint64_t kInvalidConnectionTimeoutSec = 10;

    std::shared_ptr<std::thread> run_thread_{ nullptr };
    uv_udp_t* handle_{ nullptr };
    std::unordered_map<std::string, ex_uv_tcp_t*> conn_map_;
    // Peers for which a uv_tcp_connect is in flight but on_connect has not yet
    // fired. Used to prevent duplicate concurrent connection attempts in the same
    // uv_async_cb iteration.
    std::unordered_set<std::string> pending_conns_;
    std::string ip_port_;
    int backlog_;
    bool create_server_{ false };
    std::string msg_random_;
    uint64_t thread_msg_count_[common::kMaxThreadCount] = { 0 };
    std::shared_ptr<std::thread> output_thread_ = nullptr;
    std::condition_variable output_con_;
    std::mutex output_mutex_;
    std::unordered_map<std::string, int32_t> ip_socket_map_;
    std::atomic<bool> destroy_ = false;
    std::queue<ex_uv_tcp_t*> invalid_conns_;
    NetworkDelaySimulator network_delay_simulator_;

    DISALLOW_COPY_AND_ASSIGN(TcpTransport);
};
}  // namespace transport

}  // namespace shardora

#endif