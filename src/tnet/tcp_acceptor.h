#pragma once

#include <atomic>
#include<unordered_map>

#include "common/spin_mutex.h"
#include "tnet/tnet_utils.h"
#include "tnet/event/event_handler.h"
#include "tnet/event/event_loop.h"
#include "tnet/socket/socket.h"
#include "tnet/socket/server_socket.h"
#include "tnet/utils/packet_factory.h"

namespace zjchain {

namespace tnet {

class TcpAcceptor : public EventHandler {
public:
    bool Start();
    bool Stop();
    void Destroy();
    bool SetListenSocket(Socket& socket);
    EventLoop& GetNextEventLoop() const;
    TcpAcceptor(
            uint32_t recv_buff_size,
            uint32_t send_buff_size,
            PacketHandler pakcet_handler,
            ConnectionHandler conn_handler,
            PacketFactory* packet_factory,
            EventLoop& event_loop,
            const std::vector<EventLoop*>& event_loops);
    virtual ~TcpAcceptor();

private:
    virtual bool ImplResourceInit();
    virtual void ImplResourceDestroy();
    virtual TcpConnection* CreateTcpConnection(
            EventLoop& evnetLoop,
            ServerSocket& socket);
    void ReleaseByIOThread();
    virtual bool OnRead();
    virtual void OnWrite();

    mutable common::SpinMutex mutex_;
    Socket* socket_{ nullptr };
    volatile bool stop_{ true };
    mutable uint32_t round_robin_index_{ 0 };
    uint32_t recv_buff_size_{ 0 };
    uint32_t send_buff_size_{ 0 };
    PacketHandler pakcet_handler_;
    ConnectionHandler conn_handler_;
    PacketFactory* packet_factory_{ nullptr };
    EventLoop& event_loop_;
    std::vector<EventLoop*> event_loops_;
    std::atomic<uint32_t> destroy_{ 0 };

    DISALLOW_COPY_AND_ASSIGN(TcpAcceptor);
};

}  // namespace tnet

}  // namespace zjchain
