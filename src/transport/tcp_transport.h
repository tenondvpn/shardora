#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>

#include "common/limit_hash_set.h"
#include "common/spin_mutex.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
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

namespace shardora {

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
        const std::string& ip,
        uint16_t port,
        const transport::protobuf::Header& message);
    int Send(
        tnet::TcpInterface* conn,
        const transport::protobuf::Header& message);
    int Send(
        tnet::TcpInterface* conn,
        const std::string& message);
    int GetSocket();
    std::string GetHeaderHashForSign(const transport::protobuf::Header& message);
    void SetMessageHash(const transport::protobuf::Header& message);

    // remove later
    void SetMessageHash(const transport::protobuf::OldHeader& message);
    int Send(
        const std::string& ip,
        uint16_t port,
        const transport::protobuf::OldHeader& message);
    void AddLocalMessage(transport::MessagePtr msg_ptr);

private:
    TcpTransport();
    ~TcpTransport();
    bool OnClientPacket(std::shared_ptr<tnet::TcpConnection> conn, tnet::Packet& packet);
    void CreateDropNodeMessage(const std::string& ip, uint16_t port);
    void Output();
    std::shared_ptr<tnet::TcpConnection> GetConnection(
        const std::string& ip,
        uint16_t port);
    void CheckConnectionValid();

    static const uint64_t kEraseConnPeriod = 10000000lu;
    static const uint32_t kEachCheckConnectionCount = 100u;
    static const uint64_t kCheckEraseConnPeriodMs = 10000lu;

    std::shared_ptr<tnet::TnetTransport> transport_{ nullptr };
    tnet::TcpAcceptor* acceptor_{ nullptr };
    EncoderFactory encoder_factory_;
    tnet::ListenSocket* socket_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<tnet::TcpConnection>> conn_map_;
    MultiThreadHandler* msg_handler_ = nullptr;
    uint64_t thread_msg_count_[common::kMaxThreadCount] = { 0 };
    std::string msg_random_;
    std::atomic<bool> destroy_ = false;
    std::shared_ptr<std::thread> output_thread_ = nullptr;
    common::ThreadSafeQueue<std::shared_ptr<ClientItem>> output_queues_[common::kMaxThreadCount];
    common::ThreadSafeQueue<std::shared_ptr<tnet::TcpConnection>> from_client_conn_queues_;
    std::unordered_map<std::string, std::shared_ptr<tnet::TcpConnection>> from_conn_map_;
    common::LimitHashSet<std::shared_ptr<tnet::TcpConnection>> added_conns_{ 1024 };
    std::condition_variable output_con_;
    std::mutex output_mutex_;
    std::mutex send_output_mutex_;
    common::ThreadSafeQueue<std::shared_ptr<tnet::TcpConnection>> in_check_queue_;
    common::ThreadSafeQueue<std::shared_ptr<tnet::TcpConnection>> out_check_queue_;
    common::ThreadSafeQueue<transport::MessagePtr> local_messages_[common::kMaxThreadCount];
    std::deque<std::shared_ptr<tnet::TcpConnection>> waiting_check_queue_;
    common::Tick check_conn_tick_;
    uint32_t in_message_type_count_[common::kMaxMessageTypeCount] = { 0 };
    std::atomic<uint32_t> out_message_type_count_[common::kMaxMessageTypeCount] = { 0 };
};

}  // namespace transport

}  // namespace shardora
