#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>

#include "common/limit_hash_set.h"
#include "common/spin_mutex.h"
#include "common/thread_safe_queue.h"
#include "tnet/socket/socket_factory.h"
#include "tnet/socket/listen_socket.h"
#include "tnet/tnet_transport.h"
#include "tnet/tcp_connection.h"
#include "tnet/tcp_acceptor.h"
#include "tnet/utils/packet.h"
#include "tnet/utils/cmd_packet.h"
#include "tnet/utils/msg_packet.h"
#include "transport/msg_decoder.h"
#include "transport/msg_encoder.h"
#include "transport/encoder_factory.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace transport {

class MultiThreadHandler;
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
        uint8_t thread_idx,
        const std::string& ip,
        uint16_t port,
        const transport::protobuf::Header& message);
    int Send(
        uint8_t thread_idx,
        tnet::TcpInterface* conn,
        const transport::protobuf::Header& message);
    int Send(
        uint8_t thread_idx,
        tnet::TcpInterface* conn,
        const std::string& message);
    int GetSocket();
    std::string GetHeaderHashForSign(const transport::protobuf::Header& message);
    void SetMessageHash(const transport::protobuf::Header& message, uint8_t thread_idx);

    // remove later
    void SetMessageHash(const transport::protobuf::OldHeader& message, uint8_t thread_idx);
    int Send(
        uint8_t thread_idx,
        const std::string& ip,
        uint16_t port,
        const transport::protobuf::OldHeader& message);

private:
    TcpTransport();
    ~TcpTransport();
    bool OnClientPacket(tnet::TcpConnection* conn, tnet::Packet& packet);
    void EraseConn(uint64_t now_tm_ms);
    void Output();
    tnet::TcpConnection* GetConnection(
        const std::string& ip,
        uint16_t port);

    static const uint64_t kEraseConnPeriod = 10000000lu;
    static const uint64_t kCheckEraseConnPeriodMs = 10000lu;

    std::shared_ptr<tnet::TnetTransport> transport_{ nullptr };
    tnet::TcpAcceptor* acceptor_{ nullptr };
    std::shared_ptr<EncoderFactory> encoder_factory_ = nullptr;
    tnet::ListenSocket* socket_{ nullptr };
    std::unordered_map<std::string, tnet::TcpConnection*> conn_map_;
    std::deque<tnet::TcpConnection*> erase_conns_;
    common::SpinMutex erase_conns_mutex_;
    common::Tick erase_conn_tick_;
    MultiThreadHandler* msg_handler_ = nullptr;
    uint64_t thread_msg_count_[common::kMaxThreadCount] = { 0 };
    uint8_t server_thread_idx_ = 255;
    std::string msg_random_;
    volatile bool destroy_ = false;
    std::shared_ptr<std::thread> output_thread_ = nullptr;
    common::ThreadSafeQueue<std::shared_ptr<ClientItem>> output_queues_[common::kMaxThreadCount];
    common::ThreadSafeQueue<tnet::TcpConnection*> from_client_conn_queues_;
    std::unordered_map<std::string, tnet::TcpConnection*> from_conn_map_;
    common::LimitHashSet<tnet::TcpConnection*> added_conns_{ 1024 };
    std::condition_variable output_con_;
    std::mutex output_mutex_;
    std::mutex send_output_mutex_;
    uint64_t prev_erase_timestamp_ms_ = 0;
    common::LimitHashSet<uint64_t> sent_msgs_ = 1024 * 1024;
};

}  // namespace transport

}  // namespace zjchain
