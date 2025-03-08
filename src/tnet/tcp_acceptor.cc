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
        ZJC_ERROR("new connection handler failed, destroy connection");
        conn.Destroy(true);
    } else {
        EventLoop& event_loop = conn.GetEventLoop();
        int fd = conn.GetSocket()->GetFd();
        if (!event_loop.EnableIoEvent(fd, kEventRead | kEventWrite, conn)) {
            ZJC_ERROR("enable read event on new socket[%d] failed", fd);
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
          event_loops_(event_loops) {}

TcpAcceptor::~TcpAcceptor() {
    if (socket_ != NULL) {
        socket_->Free();
    }
}

bool TcpAcceptor::Start() {
    common::AutoSpinLock gaurd(mutex_);
    if (socket_ == NULL) {
        ZJC_ERROR("socket must be set");
        return false;
    }

    if (!stop_) {
        ZJC_ERROR("already start");
        return false;
    }

    if (!ImplResourceInit()) {
        ZJC_ERROR("impl resouce init failed");
        return false;
    }

    bool rc = event_loop_.EnableIoEvent(socket_->GetFd(), kEventRead, *this);
    if (rc) {
        stop_ = false;
        ZJC_INFO("enable accept event success");
    } else {
        ZJC_ERROR("enable accept event failed");
    }

    check_conn_tick_.CutOff(
        10000000, 
        std::bind(&TcpAcceptor::CheckConnectionValid, this));
    return rc;
}

bool TcpAcceptor::Stop() {
    common::AutoSpinLock gaurd(mutex_);
    if (socket_ == NULL) {
        ZJC_ERROR("socket must be set");
        return false;
    }

    if (stop_) {
        ZJC_ERROR("already stop");
        return false;
    }

    ImplResourceDestroy();
    bool rc = event_loop_.DisableIoEvent(socket_->GetFd(), kEventRead, *this);
    if (rc) {
        stop_ = true;
        ZJC_ERROR("disable accept event success");
    } else {
        ZJC_ERROR("disable accept event failed");
    }

    return rc;
}

void TcpAcceptor::Destroy() {
    destroy_ = true;
    event_loop_.PostTask(std::bind(&TcpAcceptor::ReleaseByIOThread, this));
}

bool TcpAcceptor::SetListenSocket(Socket& socket) {
    common::AutoSpinLock gaurd(mutex_);
    if (socket_ != NULL) {
        ZJC_ERROR("listen socket already set");
        return false;
    }

    socket_ = &socket;
    return true;
}

EventLoop& TcpAcceptor::GetNextEventLoop() const {
    size_t index = round_robin_index_++ % event_loops_.size();
    return *event_loops_[index];
}

void TcpAcceptor::ReleaseByIOThread() {}

bool TcpAcceptor::OnRead() {
    ListenSocket* listenSocket = dynamic_cast<ListenSocket*>(socket_);
    if (listenSocket == NULL) {
        ZJC_ERROR("cast to TcpListenSocket failed");
        return false;
    }

    while (!stop_) {
        ServerSocket* socket = NULL;
        if (!listenSocket->Accept(&socket)) {
            break;
        }

        if (!socket->SetNonBlocking(true)) {
            ZJC_ERROR("set nonblocking failed, close socket");
            socket->Free();
            continue;
        }

        if (!socket->SetCloseExec(true)) {
            ZJC_ERROR("set close exec failed");
        }

        if (recv_buff_size_ != 0 && !socket->SetSoRcvBuf(recv_buff_size_)) {
            ZJC_ERROR("set recv buffer size failed");
        }

        if (send_buff_size_ != 0 && !socket->SetSoSndBuf(send_buff_size_)) {
            ZJC_ERROR("set send buffer size failed");
        }
        EventLoop& event_loop = GetNextEventLoop();
        auto conn = CreateTcpServerConnection(event_loop, *socket);
        if (conn == nullptr) {
            ZJC_ERROR("create connection failed, close socket[%d]",
                socket->GetFd());
            socket->Free();
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
        std::string from_ip;
        uint16_t from_port;
        socket->GetIpPort(&from_ip, &from_port);
        conn_map_[from_ip + std::to_string(from_port)] = conn;
        CHECK_MEMORY_SIZE(conn_map_);
        in_check_queue_.push(conn);
        while (!destroy_) {
            std::shared_ptr<TcpConnection> out_conn = nullptr;
            if (!out_check_queue_.pop(&out_conn) || out_conn == nullptr) {
                break;
            }

            auto key = out_conn->socket_ip() + std::to_string(out_conn->socket_port());
            auto iter = conn_map_.find(key);
            if (iter != conn_map_.end()) {
                conn_map_.erase(iter);
                CHECK_MEMORY_SIZE(conn_map_);
                ZJC_INFO("remove accept connection: %s", key.c_str());
            }
        }
    }

    return false;
}

void TcpAcceptor::CheckConnectionValid() {
    while (!destroy_) {
        std::shared_ptr<TcpConnection> out_conn = nullptr;
        if (!in_check_queue_.pop(&out_conn) || out_conn == nullptr) {
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
        ZJC_DEBUG("ShouldReconnect called now checked stopted conn waiting_check_queue_ size: %u", waiting_check_queue_.size());
        if (conn->CheckStoped()) {
            ZJC_DEBUG("checked stopted conn.");
            out_check_queue_.push(conn);
        } else {
            waiting_check_queue_.push_back(conn);
        }
    }

    check_conn_tick_.CutOff(
        10000000, 
        std::bind(&TcpAcceptor::CheckConnectionValid, this));
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
        ServerSocket& socket) {
    auto conn = std::make_shared<TcpConnection>(event_loop);
    conn->SetSocket(socket);
    return conn;
}

}  // namespace tnet

}  // namespace shardora
