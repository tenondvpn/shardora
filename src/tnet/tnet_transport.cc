#include "tnet/tnet_transport.h"

#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>

#include "tnet/tcp_connection.h"
#include "tnet/socket/socket_factory.h"

namespace shardora {

namespace tnet {

TnetTransport::TnetTransport(
        bool acceptor_isolate_thread,
        uint32_t recv_buff_size,
        uint32_t send_buff_size,
        uint32_t thread_count,
        PacketHandler packet_handler,
        PacketFactory* packet_factory)
        : acceptor_isolate_thread_(acceptor_isolate_thread),
          recv_buff_size_(recv_buff_size),
          send_buff_size_(send_buff_size),
          thread_count_(thread_count),
          packet_handler_(packet_handler),
          packet_factory_(packet_factory) {
    //assert(packet_factory != nullptr);
    //assert(recv_buff_size > 1024u);
    //assert(send_buff_size > 1024u);
    //assert(thread_count > 0);
}

TnetTransport::~TnetTransport() {}

std::shared_ptr<TcpConnection> TnetTransport::CreateConnection(
        const std::string& peer_spec,
        const std::string& local_spec,
        uint32_t timeout) {
    auto socket = SocketFactory::CreateTcpClientSocket(peer_spec, local_spec);
    if (socket == NULL) {
        SHARDORA_ERROR("create tcp client socket failed");
        return NULL;
    }

    // 1. Set non-blocking
    if (!socket->SetNonBlocking(true)) {
        SHARDORA_ERROR("set non-blocking failed");
        socket->Free();
        return NULL;
    }

    // 2. Set CloseExec
    if (!socket->SetCloseExec(true)) {
        SHARDORA_ERROR("set close-exec failed");
    }

    // 3. [New] Enable KeepAlive to maintain long connection activity
    int keep_alive = 1;
    if (setsockopt(socket->GetFd(), SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) < 0) {
        SHARDORA_WARN("set SO_KEEPALIVE failed, errno: %d", errno);
    }

    // 3b. Enable TCP_NODELAY for low-latency consensus messages.
    // Without this, Nagle's algorithm can buffer small consensus messages
    // (votes, proposals) for up to 200ms, causing consensus timeouts.
    if (!socket->SetTcpNoDelay(true)) {
        SHARDORA_WARN("set TCP_NODELAY failed");
    }

    // 4. [Critical Fix] Set Linger option for graceful shutdown
    // l_onoff=1, l_linger=1: When close() is called, if there is data in the buffer, 
    // wait 1 second to finish sending before closing, sending FIN.
    // This effectively prevents sending RST directly, which causes getpeername error on the server side.
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 1; 
    if (setsockopt(socket->GetFd(), SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) < 0) {
        SHARDORA_WARN("set SO_LINGER failed, errno: %d", errno);
    }

    // 5. Set buffer size
    if (recv_buff_size_ != 0  && !socket->SetSoRcvBuf(recv_buff_size_)) {
        SHARDORA_ERROR("set recv buf failed");
    }
    if (send_buff_size_ != 0 && !socket->SetSoSndBuf(send_buff_size_)) {
        SHARDORA_ERROR("set send buf failed");
    }

    auto conn = CreateTcpConnection(GetNextEventLoop(), socket);
    if (conn == nullptr) {
        SHARDORA_ERROR("create tcp connection failed");
        return nullptr;
    }

    conn->SetPacketHandler(packet_handler_);
    conn->SetPacketEncoder(packet_factory_->CreateEncoder());
    conn->SetPacketDecoder(packet_factory_->CreateDecoder());

    // 6. Connect
    // Update: Connect failure is common (network jitter, target down), use WARN instead of ERROR.
    if (!conn->Connect(timeout)) {
        SHARDORA_WARN("connect peer [%s] failed: %s (%d)",
                peer_spec.c_str(), strerror(errno), errno);
        
        // Use Destroy(true) to close socket immediately since connection failed.
        // No need to wait for IO thread.
        conn->Destroy(true);
        return nullptr;
    }

    return conn;
}

TcpAcceptor* TnetTransport::CreateAcceptor(ConnectionHandler conn_handler) {
    return new TcpAcceptor(
            recv_buff_size_,
            send_buff_size_,
            packet_handler_,
            conn_handler,
            packet_factory_,
            GetAcceptorEventLoop(),
            event_loop_vec_);
}

bool TnetTransport::Init() {
    if (acceptor_isolate_thread_) {
        EventLoop* acceptor_event_loop = new EventLoop;
        if (!acceptor_event_loop->Init()) {
            SHARDORA_ERROR("init acceptor event loop failed");
            delete acceptor_event_loop;
            acceptor_event_loop = nullptr;
            return false;
        }

        acceptor_event_loop_ = acceptor_event_loop;
    }

    uint32_t i;
    for (i = 0; i < thread_count_; ++i) {
        EventLoop* event_loop = new EventLoop;
        if (!event_loop->Init()) {
            SHARDORA_ERROR("init event loop failed");
            delete event_loop;
            event_loop = nullptr;
            break;
        }

        event_loop_vec_.push_back(event_loop);
        SHARDORA_DEBUG("success add event loop: %d", i);
    }

    if (i == thread_count_) {
        if (ImplResourceInit()) {
            return true;
        }
    }

    Destroy();
    return false;
}

void TnetTransport::Destroy() {
    if (acceptor_thread_ != nullptr) {
        if (acceptor_thread_->joinable()) {
            acceptor_thread_->join();
        }
        delete acceptor_thread_;
        acceptor_thread_ = nullptr;
    }

    for (size_t i = 0; i < thread_vec_.size(); i++) {
        if (thread_vec_[i] != nullptr) {
            if (thread_vec_[i]->joinable()) {
                thread_vec_[i]->join();
            }
            delete thread_vec_[i];
            thread_vec_[i] = nullptr;
        }
    }

    thread_vec_.clear();
    ImplResourceDestroy();

    if (acceptor_event_loop_ != nullptr) {
        delete acceptor_event_loop_;
        acceptor_event_loop_ = nullptr;
    }

    for (size_t i = 0; i < event_loop_vec_.size(); i++) {
        if (event_loop_vec_[i] != nullptr) {
            delete event_loop_vec_[i];
            event_loop_vec_[i] = nullptr;
        }
    }

    event_loop_vec_.clear();
}

void TnetTransport::Dispatch() {
    //assert(!event_loop_vec_.empty());
    if (!acceptor_isolate_thread_) {
        stoped_ = false;
        if (event_loop_vec_[0] != NULL) {
            event_loop_vec_[0]->Dispatch();
        }
    }
}

bool TnetTransport::Start() {
    if (!stoped_) {
        SHARDORA_ERROR("already start");
        return false;
    }

    SHARDORA_DEBUG("waiting for work_thread.");
    for (size_t i = 0; i < event_loop_vec_.size(); i++) {
        waiting_success_ = false;
        std::thread* tmp_thread = new std::thread(std::bind(
                &TnetTransport::ThreadProc,
                this,
                event_loop_vec_[i]));
        SHARDORA_DEBUG("waiting for work_thread now.");
        std::unique_lock<std::mutex> lock(mutex_);
        con_.wait_for(lock, std::chrono::milliseconds(3000), [&] { 
            return waiting_success_.load();
        });

        if (!waiting_success_) {
            SHARDORA_ERROR("waiting for work_thread failed.");
            return false;
        }
        thread_vec_.push_back(tmp_thread);
    }

    SHARDORA_DEBUG("waiting for work_thread success.");
    stoped_ = false;
    if (acceptor_event_loop_ == NULL) {
        return true;
    }

    SHARDORA_DEBUG("waiting for accept_thread.");
    waiting_success_ = false;
    acceptor_thread_ = new std::thread(std::bind(
            &TnetTransport::ThreadProc,
            this,
            acceptor_event_loop_));
    SHARDORA_DEBUG("waiting for work_thread now.");
    std::unique_lock<std::mutex> lock(mutex_);
    con_.wait_for(lock, std::chrono::milliseconds(3000), [&] { 
        return waiting_success_.load(); 
    });

    if (!waiting_success_) {
        SHARDORA_ERROR("waiting for accept_thread failed.");
        return false;
    }
    SHARDORA_DEBUG("waiting for accept_thread success.");
    return true;
}

bool TnetTransport::Stop() {
    if (stoped_) {
        return true;
    }

    if (acceptor_event_loop_ != NULL) {
        if (!acceptor_event_loop_->Shutdown()) {
            SHARDORA_ERROR("accept event loop shutdown failed");
            return false;
        }
    }

    for (size_t i = 0; i < event_loop_vec_.size(); i++) {
        EventLoop* event_loop = event_loop_vec_[i];
        if (!event_loop->Shutdown()) {
            SHARDORA_ERROR("event loop shutdown failed");
            return false;
        }
    }

    // Join threads if they exist
    if (acceptor_thread_ && acceptor_thread_->joinable()) {
        acceptor_thread_->join();
    }
    
    for (size_t i = 0; i < thread_vec_.size(); i++) {
        auto thread = thread_vec_[i];
        if (thread && thread->joinable()) {
            thread->join();
        }
    }

    stoped_ = true;
    return true;
}

const std::vector<EventLoop*>& TnetTransport::GetEventLoopVec() const {
    return event_loop_vec_;
}

EventLoop& TnetTransport::GetAcceptorEventLoop() const {
    EventLoop* event_loop = NULL;
    if (acceptor_isolate_thread_) {
        //assert(acceptor_event_loop_ != NULL);
        event_loop = acceptor_event_loop_;
    } else {
        //assert(!event_loop_vec_.empty());
        size_t index = round_robin_index_.fetch_add(1) % event_loop_vec_.size();
        event_loop = event_loop_vec_[index];
    }

    //assert(event_loop != NULL);
    return *event_loop;
}

EventLoop& TnetTransport::GetNextEventLoop() const {
    //assert(!event_loop_vec_.empty());
    size_t index = round_robin_index_.fetch_add(1) % event_loop_vec_.size();
    return *event_loop_vec_[index];
}

bool TnetTransport::ImplResourceInit() {
    return true;
}

void TnetTransport::ImplResourceDestroy() {
}

std::shared_ptr<TcpConnection> TnetTransport::CreateTcpConnection(
        EventLoop& event_loop,
        std::shared_ptr<Socket> socket) {
    auto conn = std::make_shared<TcpConnection>(event_loop);
    conn->SetSocket(socket);
    common::GlobalInfo::Instance()->AddSharedObj(16);
    return conn;
}

void TnetTransport::ThreadProc(EventLoop* event_loop) {
    {
        auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
        std::unique_lock<std::mutex> lock(mutex_);
        waiting_success_ = true;
        con_.notify_one();
    }
    
    event_loop->Dispatch();
}

}  // namespace tnet

}  // namespace shardora