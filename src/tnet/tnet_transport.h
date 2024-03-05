#pragma once

#include "tnet/tcp_acceptor.h"
#include "tnet/tcp_connection.h"
#include "tnet/socket/client_socket.h"
#include "tnet/utils/packet_factory.h"

namespace zjchain {

namespace tnet {

class TnetTransport {
public:
    TnetTransport(
            bool acceptor_isolate_thread,
            uint32_t recv_buff_size,
            uint32_t send_buff_size,
            uint32_t thread_count,
            PacketHandler packet_handler,
            PacketFactory* packet_factory);
    virtual ~TnetTransport();
    virtual TcpConnection* CreateConnection(
            const std::string& peerSpec,
            const std::string& localSpec,
            uint32_t timeout);
    virtual TcpAcceptor* CreateAcceptor(ConnectionHandler conn_handler);

    bool Init();
    void Destroy();
    void Dispatch();
    bool Start();
    bool Stop();
    const std::vector<EventLoop*>& GetEventLoopVec() const;
    EventLoop& GetAcceptorEventLoop() const;
    EventLoop& GetNextEventLoop() const;

protected:
    virtual bool ImplResourceInit();
    virtual void ImplResourceDestroy();

private:
    virtual TcpConnection* CreateTcpConnection(
            EventLoop& event_loop,
            ClientSocket& socket);
    void ThreadProc(EventLoop* event_loop);

    bool acceptor_isolate_thread_{ true };
    uint32_t recv_buff_size_{ 10 * 1024 * 1024 };
    uint32_t send_buff_size_{ 10 * 1024 * 1024 };
    uint32_t thread_count_{ 4 };
    std::vector<EventLoop*> event_loop_vec_;
    std::vector<std::thread*> thread_vec_;
    EventLoop* acceptor_event_loop_{ nullptr };
    std::thread* acceptor_thread_{ nullptr };
    mutable std::atomic<int32_t> round_robin_index_{ 0 };
    bool stoped_{ true };
    PacketHandler packet_handler_{ nullptr };
    PacketFactory* packet_factory_{ nullptr };

    DISALLOW_COPY_AND_ASSIGN(TnetTransport);
};

}  // namespace tnet

}  // namespace zjchain
