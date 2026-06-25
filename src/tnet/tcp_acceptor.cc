#include "tnet/tcp_acceptor.h"

#include "tnet/tcp_connection.h"
#include "tnet/socket/listen_socket.h"
#include "tnet/tnet_utils.h"

namespace shardora {

namespace tnet {

namespace {

void NewConnectionHandler(
        TcpConnection& conn,
        ConnectionHandler& handler) {
    if (handler && !handler(conn)) {
        SHARDORA_ERROR("new connection handler failed, destroy connection");
        conn.Destroy(true);
    } else {
        EventLoop& event_loop = conn.GetEventLoop();
        int fd = conn.GetSocket()->GetFd();
        if (!event_loop.EnableIoEvent(fd, kEventRead | kEventWrite, conn)) {
            SHARDORA_ERROR("enable read event on new socket[%d] failed", fd);
            conn.Destroy(true);
        }
    }
}

}

TcpAcceptor::TcpAcceptor(
        uint32_t recv_buff_size,
        uint32_t send_buff_size,
        PacketHandler packet_handler,
        ConnectionHandler conn_handler,
        PacketFactory* packet_factory,
        EventLoop& event_loop,
        const std::vector<EventLoop*>& event_loops)
        : recv_buff_size_(recv_buff_size),
          send_buff_size_(send_buff_size),
          pakcet_handler_(packet_handler),
          conn_handler_(conn_handler),
          packet_factory_(packet_factory),
          event_loop_(event_loop),
          event_loops_(event_loops) {
    in_check_queue_ = new common::ThreadSafeQueue<std::shared_ptr<TcpConnection>>();
    out_check_queue_ = new common::ThreadSafeQueue<std::shared_ptr<TcpConnection>>();
}

TcpAcceptor::~TcpAcceptor() {
    if (socket_ != NULL) {
        socket_->Free();
    }
}

bool TcpAcceptor::Start() {
    common::AutoSpinLock gaurd(mutex_);
    if (socket_ == NULL) {
        SHARDORA_ERROR("socket must be set");
        return false;
    }

    if (!stop_) {
        SHARDORA_ERROR("already start");
        return false;
    }

    if (!ImplResourceInit()) {
        SHARDORA_ERROR("impl resouce init failed");
        return false;
    }

    bool rc = event_loop_.EnableIoEvent(socket_->GetFd(), kEventRead, *this);
    if (rc) {
        stop_ = false;
        SHARDORA_DEBUG("enable accept event success");
    } else {
        SHARDORA_ERROR("enable accept event failed");
    }

    check_conn_tick_.CutOff(
        10000000, 
        std::bind(&TcpAcceptor::CheckConnectionValid, this));
    return rc;
}

bool TcpAcceptor::Stop() {
    common::AutoSpinLock gaurd(mutex_);
    if (socket_ == NULL) {
        SHARDORA_ERROR("socket must be set");
        return false;
    }

    if (stop_) {
        SHARDORA_ERROR("already stop");
        return false;
    }

    ImplResourceDestroy();
    bool rc = event_loop_.DisableIoEvent(socket_->GetFd(), kEventRead, *this);
    if (rc) {
        stop_ = true;
        SHARDORA_ERROR("disable accept event success");
    } else {
        SHARDORA_ERROR("disable accept event failed");
    }

    return rc;
}

void TcpAcceptor::Destroy() {
    if (destroy_) {
        return;
    }

    destroy_ = true;
    // Bug fix #9: Stop the connection check timer to prevent callbacks after destroy.
    check_conn_tick_.Destroy();
    
    event_loop_.PostTask(std::bind(&TcpAcceptor::ReleaseByIOThread, this));
    // Bug fix #10: Moved queue deletion to ReleaseByIOThread to avoid
    // deleting queues while CheckConnectionValid may still be accessing them.
    // The queues are now cleaned up on the IO thread after all pending tasks complete.
}

bool TcpAcceptor::SetListenSocket(Socket& socket) {
    common::AutoSpinLock gaurd(mutex_);
    if (socket_ != NULL) {
        SHARDORA_ERROR("listen socket already set");
        return false;
    }

    socket_ = &socket;
    return true;
}

EventLoop& TcpAcceptor::GetNextEventLoop() const {
    size_t index = round_robin_index_++ % event_loops_.size();
    return *event_loops_[index];
}

// Bug fix #11: ReleaseByIOThread was empty - now properly cleans up resources.
void TcpAcceptor::ReleaseByIOThread() {
    // Clear connection map to release shared_ptr references
    conn_map_.clear();
    waiting_check_queue_.clear();
    
    if (in_check_queue_) {
        delete in_check_queue_;
        in_check_queue_ = nullptr;
    }

    if (out_check_queue_) {
        delete out_check_queue_;
        out_check_queue_ = nullptr;
    }
}

bool TcpAcceptor::OnRead() {
    ListenSocket* listenSocket = dynamic_cast<ListenSocket*>(socket_);
    if (listenSocket == NULL) {
        SHARDORA_ERROR("cast to TcpListenSocket failed");
        return false;
    }

    while (!stop_) {
        auto socket = listenSocket->Accept();
        if (!socket) {
            break;
        }

        auto svr_socket = std::dynamic_pointer_cast<ServerSocket>(socket);
        std::string from_ip = InAddrToString(svr_socket->peer_addr());
        uint16_t from_port = svr_socket->peer_port();
        if (!socket->SetNonBlocking(true)) {
            SHARDORA_ERROR("set nonblocking failed, close socket");
            // Bug fix #12: socket is a shared_ptr, don't call Free() manually.
            // Let the shared_ptr destructor handle cleanup.
            continue;
        }

        if (!socket->SetCloseExec(true)) {
            SHARDORA_ERROR("set close exec failed");
        }

        if (recv_buff_size_ != 0 && !socket->SetSoRcvBuf(recv_buff_size_)) {
            SHARDORA_ERROR("set recv buffer size failed");
        }

        if (send_buff_size_ != 0 && !socket->SetSoSndBuf(send_buff_size_)) {
            SHARDORA_ERROR("set send buffer size failed");
        }

        // Enable TCP_NODELAY on accepted connections for low-latency consensus.
        if (!socket->SetTcpNoDelay(true)) {
            SHARDORA_ERROR("set tcp nodelay failed on accepted socket");
        }

        EventLoop& event_loop = GetNextEventLoop();
        auto conn = CreateTcpServerConnection(event_loop, socket);
        if (conn == nullptr) {
            SHARDORA_ERROR("create connection failed, close socket[%d]",
                socket->GetFd());
            // Bug fix #13: Same as above - shared_ptr handles cleanup.
            continue;
        }

        conn->SetTcpState(TcpConnection::kTcpConnected);
        if (pakcet_handler_) {
            conn->SetPacketHandler(pakcet_handler_);
        }

        conn->SetPacketEncoder(packet_factory_->CreateEncoder());
        conn->SetPacketDecoder(packet_factory_->CreateDecoder());
        event_loop.PostTask(std::bind(
            &NewConnectionHandler,
            std::ref(*conn),
            std::ref(conn_handler_)));
        event_loop.Wakeup();
        SHARDORA_DEBUG("accept success %s:%d", from_ip.c_str(), from_port);
        conn_map_[from_ip + std::to_string(from_port)] = conn;
        if (in_check_queue_) {
            in_check_queue_->push(conn);
        }
        int cleanup_limit = 32;
        while (!destroy_ && cleanup_limit-- > 0) {
            std::shared_ptr<TcpConnection> out_conn = nullptr;
            if (!out_check_queue_ || !out_check_queue_->pop(&out_conn) || out_conn == nullptr) {
                break;
            }

            auto key = out_conn->socket_ip() + std::to_string(out_conn->socket_port());
            auto iter = conn_map_.find(key);
            if (iter != conn_map_.end()) {
                conn_map_.erase(iter);
                SHARDORA_DEBUG("remove accept connection: %s", key.c_str());
            }
        }
    }

    return false;
}

void TcpAcceptor::CheckConnectionValid() {
    // Bug fix #14: Check destroy_ and queue validity before accessing.
    if (destroy_ || in_check_queue_ == nullptr || out_check_queue_ == nullptr) {
        return;
    }

    while (!destroy_) {
        std::shared_ptr<TcpConnection> out_conn = nullptr;
        if (!in_check_queue_ || !in_check_queue_->pop(&out_conn) || out_conn == nullptr) {
            break;
        }

        waiting_check_queue_.push_back(out_conn);
    }

    uint32_t length = waiting_check_queue_.size();
    uint32_t check_count = 0;
    while (check_count < kEachCheckConnectionCount && check_count < length && !destroy_) {
        ++check_count;
        auto conn = waiting_check_queue_.front();
        waiting_check_queue_.pop_front();
        conn->ShouldReconnect();
        SHARDORA_DEBUG("ShouldReconnect called now checked stopted conn waiting_check_queue_ size: %u", (unsigned)waiting_check_queue_.size());
        if (conn->CheckStoped()) {
            SHARDORA_DEBUG("checked stopted conn.");
            if (out_check_queue_) {
                out_check_queue_->push(conn);
            }
        } else {
            waiting_check_queue_.push_back(conn);
        }
    }

    if (!destroy_) {
        check_conn_tick_.CutOff(
            10000000, 
            std::bind(&TcpAcceptor::CheckConnectionValid, this));
    }
}

void TcpAcceptor::OnWrite()
{
}

bool TcpAcceptor::ImplResourceInit()
{
    return true;
}

void TcpAcceptor::ImplResourceDestroy()
{
}

std::shared_ptr<TcpConnection> TcpAcceptor::CreateTcpServerConnection(
        EventLoop& event_loop,
        std::shared_ptr<Socket> socket) {
    auto conn = std::make_shared<TcpConnection>(event_loop);
    conn->SetSocket(socket);
    common::GlobalInfo::Instance()->AddSharedObj(17);
    return conn;
}

}  // namespace tnet

}  // namespace shardora
