#pragma once

#ifdef SHARDORA_USE_UV
#include <memory>
#include <queue>
#include <set>
#include <unordered_map>

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

struct ex_uv_tcp_t {
    uv_tcp_t uv_tcp;
    MsgDecoder* msg_decoder;
    char ip[64];
    uint16_t port;
    uint64_t timeout;
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