#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <unordered_map>

#include "common/spin_mutex.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "tnet/tnet_utils.h"
#include "tnet/event/event_handler.h"
#include "tnet/event/event_loop.h"
#include "tnet/socket/socket.h"
#include "tnet/socket/server_socket.h"
#include "tnet/utils/packet_factory.h"

namespace shardora {

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
    std::shared_ptr<TcpConnection> CreateTcpServerConnection(
            EventLoop& evnetLoop,
            std::shared_ptr<Socket> socket);
    void ReleaseByIOThread();
    virtual bool OnRead();
    virtual void OnWrite();
    void CheckConnectionValid();

    static const uint32_t kEachCheckConnectionCount = 100u;

    mutable common::SpinMutex mutex_;
    Socket* socket_{ nullptr };
    std::atomic<bool> stop_{ true };
    mutable uint32_t round_robin_index_{ 0 };
    uint32_t recv_buff_size_{ 0 };
    uint32_t send_buff_size_{ 0 };
    PacketHandler pakcet_handler_;
    ConnectionHandler conn_handler_;
    PacketFactory* packet_factory_{ nullptr };
    EventLoop& event_loop_;
    std::vector<EventLoop*> event_loops_;
    std::atomic<bool> destroy_ = false;
    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> conn_map_;
    common::ThreadSafeQueue<std::shared_ptr<TcpConnection>>* in_check_queue_ = nullptr;
    common::ThreadSafeQueue<std::shared_ptr<TcpConnection>>* out_check_queue_ = nullptr;
    std::deque<std::shared_ptr<TcpConnection>> waiting_check_queue_;
    common::Tick check_conn_tick_;

    DISALLOW_COPY_AND_ASSIGN(TcpAcceptor);
};

}  // namespace tnet

}  // namespace shardora
