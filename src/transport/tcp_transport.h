#pragma once

#include <memory>
#include <unordered_map>
#include <set>

#include "common/spin_mutex.h"
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
    int GetSocket();
    void FreeConnection(uint8_t thread_idx, const std::string& ip, uint16_t port);
    tnet::TcpConnection* GetConnection(
        uint8_t thread_idx,
        const std::string& ip,
        uint16_t port);
//     tnet::TcpConnection* CreateConnection(const std::string& ip, uint16_t port);
    std::string GetHeaderHashForSign(const transport::protobuf::Header& message);
    void SetMessageHash(const transport::protobuf::Header& message, uint8_t thread_idx);

private:
    TcpTransport();
    ~TcpTransport();
    bool OnClientPacket(tnet::TcpConnection* conn, tnet::Packet& packet);
    void EraseConn(uint8_t thread_idx);
    void CreateDropNodeMessage(const std::string& ip, uint16_t port);

    static const uint64_t kEraseConnPeriod = 10000000lu;

    std::shared_ptr<tnet::TnetTransport> transport_{ nullptr };
    tnet::TcpAcceptor* acceptor_{ nullptr };
    EncoderFactory encoder_factory_;
    tnet::ListenSocket* socket_{ nullptr };
    std::unordered_map<std::string, tnet::TcpConnection*> conn_map_[common::kMaxThreadCount];
    std::deque<tnet::TcpConnection*> erase_conns_;
    common::SpinMutex erase_conns_mutex_;
    common::Tick erase_conn_tick_;
    MultiThreadHandler* msg_handler_ = nullptr;
    uint64_t thread_msg_count_[common::kMaxThreadCount] = { 0 };
    uint8_t server_thread_idx_ = 255;
    std::string msg_random_;
};

}  // namespace transport

}  // namespace zjchain