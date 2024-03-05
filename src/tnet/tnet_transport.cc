#include "tnet/tnet_transport.h"

#include "tnet/tcp_connection.h"
#include "tnet/socket/socket_factory.h"

namespace zjchain {

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
    assert(packet_factory != nullptr);
    assert(recv_buff_size > 1024u);
    assert(send_buff_size > 1024u);
    assert(thread_count > 0);
}

TnetTransport::~TnetTransport() {}

TcpConnection* TnetTransport::CreateConnection(
        const std::string& peer_spec,
        const std::string& local_spec,
        uint32_t timeout) {
    ClientSocket* socket = SocketFactory::CreateTcpClientSocket(peer_spec, local_spec);
    if (socket == NULL) {
        ZJC_ERROR("create tcp client socket failed");
        return NULL;
    }

    if (!socket->SetNonBlocking(true)) {
        ZJC_ERROR("set non-blocking failed");
        socket->Free();
        return NULL;
    }

    if (!socket->SetCloseExec(true)) {
        ZJC_ERROR("set close-exec failed");
    }

    if (recv_buff_size_ != 0  && !socket->SetSoRcvBuf(recv_buff_size_)) {
        ZJC_ERROR("set recv buf failed");
    }

    if (send_buff_size_ != 0 && !socket->SetSoSndBuf(send_buff_size_)) {
        ZJC_ERROR("set recv buf failed");
    }

    TcpConnection* conn = CreateTcpConnection(GetNextEventLoop(), *socket);
    if (conn == NULL) {
        ZJC_ERROR("create tcp connection failed");
        socket->Free();
        return NULL;
    }

    conn->SetPacketHandler(packet_handler_);
    conn->SetPacketEncoder(packet_factory_->CreateEncoder());
    conn->SetPacketDecoder(packet_factory_->CreateDecoder());
    if (!conn->Connect(timeout)) {
        ZJC_ERROR("connect peer [%s] failed[%d][%s]",
                peer_spec.c_str(), errno, strerror(errno));
        conn->Destroy(true);
        return NULL;
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
            ZJC_ERROR("init acceptor event loop failed");
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
            ZJC_ERROR("init event loop failed");
            delete event_loop;
            event_loop = nullptr;
            break;
        }

        event_loop_vec_.push_back(event_loop);
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
        delete acceptor_thread_;
        acceptor_thread_ = nullptr;
    }

    for (size_t i = 0; i < thread_vec_.size(); i++) {
        if (thread_vec_[i] != nullptr) {
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
    assert(!event_loop_vec_.empty());
    if (!acceptor_isolate_thread_) {
        stoped_ = false;
        if (event_loop_vec_[0] != NULL) {
            event_loop_vec_[0]->Dispatch();
        }
    }
}

bool TnetTransport::Start() {
    if (!stoped_) {
        ZJC_ERROR("already start");
        return false;
    }

    for (size_t i = 0; i < event_loop_vec_.size(); i++) {
        std::thread* tmp_thread = new std::thread(std::bind(
                &TnetTransport::ThreadProc,
                this,
                event_loop_vec_[i]));
//         tmp_thread->detach();
        thread_vec_.push_back(tmp_thread);
    }

    stoped_ = false;
    if (acceptor_event_loop_ == NULL) {
        return true;
    }

    acceptor_thread_ = new std::thread(std::bind(
            &TnetTransport::ThreadProc,
            this,
            acceptor_event_loop_));
//     acceptor_thread_->detach();
    return true;
}

bool TnetTransport::Stop() {
    if (stoped_) {
        return true;
    }

    if (acceptor_event_loop_ != NULL) {
        if (!acceptor_event_loop_->Shutdown()) {
            ZJC_ERROR("accept event loop shutdown failed");
            return false;
        }
    }

    for (size_t i = 0; i < event_loop_vec_.size(); i++) {
        EventLoop* event_loop = event_loop_vec_[i];
        if (!event_loop->Shutdown()) {
            ZJC_ERROR("event loop shutdown failed");
            return false;
        }
    }

    acceptor_thread_->join();
    for (size_t i = 0; i < thread_vec_.size(); i++) {
        auto thread = thread_vec_[i];
        thread->join();
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
        assert(acceptor_event_loop_ != NULL);
        event_loop = acceptor_event_loop_;
    } else {
        assert(!event_loop_vec_.empty());
        size_t index = round_robin_index_.fetch_add(1) % event_loop_vec_.size();
        event_loop = event_loop_vec_[index];
    }

    assert(event_loop != NULL);
    return *event_loop;
}

EventLoop& TnetTransport::GetNextEventLoop() const {
    assert(!event_loop_vec_.empty());
    size_t index = round_robin_index_.fetch_add(1) % event_loop_vec_.size();
    return *event_loop_vec_[index];
}

bool TnetTransport::ImplResourceInit() {
    return true;
}

void TnetTransport::ImplResourceDestroy() {
}

TcpConnection* TnetTransport::CreateTcpConnection(
        EventLoop& event_loop,
        ClientSocket& socket) {
    TcpConnection* conn = new TcpConnection(event_loop);
    conn->SetSocket(socket);
    return conn;
}

void TnetTransport::ThreadProc(EventLoop* event_loop) {
    event_loop->Dispatch();
}

}  // namespace tnet

}  // namespace zjchain
