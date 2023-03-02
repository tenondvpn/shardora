#include "transport/tcp_transport.h"

#include "common/global_info.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "transport/transport_utils.h"
#include "transport/multi_thread.h"
#include "protos/get_proto_hash.h"
#include "protos/transport.pb.h"

namespace zjchain {

namespace transport {

using namespace tnet;
TcpTransport* TcpTransport::Instance() {
    static TcpTransport ins;
    return &ins;
}

TcpTransport::TcpTransport() {
    for (int32_t i = 0; i < common::kMaxThreadCount; i++) {
        thread_msg_count_[i] = common::TimeUtils::TimestampUs();
    }

    erase_conn_tick_.CutOff(
        kEraseConnPeriod,
        std::bind(&TcpTransport::EraseConn, this, std::placeholders::_1));
}

TcpTransport::~TcpTransport() {}

int TcpTransport::Init(
        const std::string& ip_port,
        int backlog,
        bool create_server,
        MultiThreadHandler* msg_handler) {
    // server just one thread
    msg_random_ = common::Random::RandomString(32);
    server_thread_idx_ = common::GlobalInfo::Instance()->message_handler_thread_count();
    msg_handler_ = msg_handler;
    auto packet_handler = std::bind(
            &TcpTransport::OnClientPacket,
            this,
            std::placeholders::_1,
            std::placeholders::_2);
    transport_ = std::make_shared<TnetTransport>(
        true,
        10 * 1024 * 1024,
        10 * 1024 * 1024,
        1,
        packet_handler,
        &encoder_factory_);
    if (!transport_->Init()) {
        TRANSPORT_ERROR("transport init failed");
        return kTransportError;
    }

    if (!create_server) {
        return kTransportSuccess;
    }

    acceptor_ = dynamic_cast<TcpAcceptor*>(transport_->CreateAcceptor(nullptr));
    if (acceptor_ == NULL) {
        TRANSPORT_ERROR("create acceptor failed");
        return kTransportError;
    }

    socket_ = SocketFactory::CreateTcpListenSocket(ip_port);
    if (socket_ == NULL) {
        TRANSPORT_ERROR("create socket failed");
        return kTransportError;
    }

    if (!socket_->SetNonBlocking(true) || !socket_->SetCloseExec(true)) {
        TRANSPORT_ERROR("set non-blocking or close-exec failed");
        return kTransportError;
    }

    if (!socket_->Listen(backlog)) {
        TRANSPORT_ERROR("listen socket failed");
        return kTransportError;
    }

    acceptor_->SetListenSocket(*socket_);
    if (!acceptor_->Start()) {
        TRANSPORT_ERROR("start acceptor failed");
        return kTransportError;
    }

    return kTransportSuccess;
}

int TcpTransport::Start(bool hold) {
    transport_->Dispatch();
    if (!transport_->Start()) {
        return kTransportError;
    }

    return kTransportSuccess;
}

void TcpTransport::Stop() {
    if (acceptor_ != nullptr) {
        acceptor_->Stop();
        acceptor_->Destroy();
    }

    if (transport_) {
        transport_->Stop();
        transport_->Destroy();
    }
}

bool TcpTransport::OnClientPacket(tnet::TcpConnection* conn, tnet::Packet& packet) {
    auto tcp_conn = dynamic_cast<tnet::TcpConnection*>(conn);
    if (conn->GetSocket() == nullptr) {
        packet.Free();
        return false;
    }

    std::string from_ip;
    uint16_t from_port;
    conn->GetSocket()->GetIpPort(&from_ip, &from_port);
    if (packet.IsCmdPacket()) {
        if (packet.PacketType() == tnet::CmdPacket::CT_TCP_NEW_CONNECTION) {
            // add connection
            packet.Free();
            return true;
        }

        if (!(from_ip.empty() || from_port == 0)) {
            FreeConnection(
                server_thread_idx_,
                from_ip,
                from_port);
        }
        
        if (!conn->PeerIp().empty() && conn->PeerPort() != 0) {
            CreateDropNodeMessage(conn->PeerIp(), conn->PeerPort());
        }

        packet.Free();
        return false;
    }

    // network message must free memory
    tnet::MsgPacket* msg_packet = dynamic_cast<tnet::MsgPacket*>(&packet);
    char* data = nullptr;
    uint32_t len = 0;
    msg_packet->GetMessageEx(&data, &len);
    if (len >= kTcpBuffLength) {
        return false;
    }

    MessagePtr msg_ptr = std::make_shared<TransportMessage>();
    if (!msg_ptr->header.ParseFromArray(data, len)) {
        TRANSPORT_ERROR("Message ParseFromString from string failed!"
            "[%s:%d][len: %d]",
            from_ip.c_str(), from_port, len);
        return false;
    }

    conn->SetPeerIp(from_ip);
    conn->SetPeerPort(from_port);
    msg_ptr->conn = conn;
//     std::cout << "handle client message: " << from_ip << ":" << from_port << ", " << conn->thread_idx() << std::endl;
    msg_handler_->HandleMessage(msg_ptr);
//     AddClientConnection(tcp_conn);
    packet.Free();
    return true;
}

void TcpTransport::CreateDropNodeMessage(const std::string& ip, uint16_t port) {
    MessagePtr msg_ptr = std::make_shared<TransportMessage>();
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(0);
    common::DhtKey dht_key;
    dht_key.construct.net_id = 0;
    msg.set_des_dht_key(std::string(dht_key.dht_key, sizeof(dht_key.dht_key)));
    msg.set_type(common::kNetworkMessage);
    SetMessageHash(msg, 0);
    auto* net_msg = msg.mutable_network_proto();
    auto drop_req = net_msg->mutable_drop_node();
    drop_req->set_ip(ip);
    drop_req->set_port(port);
    msg_handler_->HandleMessage(msg_ptr);
}

void TcpTransport::SetMessageHash(
        const transport::protobuf::Header& message,
        uint8_t thread_idx) {
    auto tmpHeader = const_cast<transport::protobuf::Header*>(&message);
    auto hash = common::Hash::Hash64(
        msg_random_ +
        std::to_string(thread_idx) +
        std::to_string(++thread_msg_count_[thread_idx]));
    tmpHeader->set_hash64(hash);
}

int TcpTransport::Send(
        uint8_t thread_idx,
        tnet::TcpInterface* tcp_conn,
        const transport::protobuf::Header& message) {
    std::string msg;
    if (!message.has_hash64() || message.hash64() == 0) {
        SetMessageHash(message, thread_idx);
    }

    message.SerializeToString(&msg);
    if (tcp_conn->Send(msg) != 0) {
        FreeConnection(thread_idx, tcp_conn->PeerIp(), tcp_conn->PeerPort());
        return kTransportError;
    }

    return kTransportSuccess;
}

int TcpTransport::Send(
        uint8_t thread_idx,
        const std::string& des_ip,
        uint16_t des_port,
        const transport::protobuf::Header& message) {
    std::string msg;
    if (!message.has_hash64() || message.hash64() == 0) {
        SetMessageHash(message, thread_idx);
    }

    message.SerializeToString(&msg);
    auto tcp_conn = GetConnection(thread_idx, des_ip, des_port);
    if (tcp_conn == nullptr) {
        TRANSPORT_ERROR("get tcp connection failed[%s][%d][hash64: %llu]",
            des_ip.c_str(), des_port, message.hash64());
        return kTransportError;
    }

    if (tcp_conn->Send(msg) != 0) {
        FreeConnection(thread_idx, des_ip, des_port);
        return kTransportError;
    }

    return kTransportSuccess;
}

int TcpTransport::GetSocket() {
    return socket_->GetFd();
}

void TcpTransport::FreeConnection(uint8_t thread_idx, const std::string& ip, uint16_t port) {
    std::string peer_spec = ip + ":" + std::to_string(port);
    auto iter = conn_map_[thread_idx].find(peer_spec);
    if (iter != conn_map_[thread_idx].end()) {
        iter->second->Destroy(true);
        {
            common::AutoSpinLock guard(erase_conns_mutex_);
            erase_conns_.push_back(iter->second);
        }
        conn_map_[thread_idx].erase(iter);
    }
}

tnet::TcpConnection* TcpTransport::CreateConnection(const std::string& ip, uint16_t port) {
    if (ip == "0.0.0.0") {
        return nullptr;
    }

    std::string peer_spec = ip + ":" + std::to_string(port);
//     std::string local_spec = common::GlobalInfo::Instance()->config_local_ip() + ":" +
//         std::to_string(common::GlobalInfo::Instance()->config_local_port());
    return transport_->CreateConnection(
            peer_spec,
            "",
            300u * 1000u * 1000u);
}

tnet::TcpConnection* TcpTransport::GetConnection(
        uint8_t thread_idx,
        const std::string& ip,
        uint16_t port) {
    if (ip == "0.0.0.0" || port == 0) {
        return nullptr;
    }

    std::string peer_spec = ip + ":" + std::to_string(port);
    auto iter = conn_map_[thread_idx].find(peer_spec);
    if (iter != conn_map_[thread_idx].end()) {
        if (iter->second->GetTcpState() == tnet::TcpConnection::kTcpClosed) {
            common::AutoSpinLock guard(erase_conns_mutex_);
            erase_conns_.push_back(iter->second);
            conn_map_[thread_idx].erase(iter);
        } else {
            return iter->second;
        }
    }

//     std::string local_spec = common::GlobalInfo::Instance()->config_local_ip() + ":" +
//         std::to_string(common::GlobalInfo::Instance()->config_local_port());
    auto tcp_conn = transport_->CreateConnection(
        peer_spec,
        "",
        3u * 1000u * 1000u);
    if (tcp_conn == nullptr) {
        return nullptr;
    }
    
    conn_map_[thread_idx][peer_spec] = tcp_conn;
    return tcp_conn;
}

// void TcpTransport::AddClientConnection(tnet::TcpConnection* conn) {
//     std::string client_ip;
//     uint16_t client_port;
//     if (conn->GetSocket()->GetIpPort(&client_ip, &client_port) != 0) {
//         return;
//     }
// 
//     std::string peer_spec = client_ip + ":" + std::to_string(client_port);
//     std::lock_guard<std::mutex> guard(conn_map_mutex_);
//     auto iter = conn_map_.find(peer_spec);
//     if (iter != conn_map_.end()) {
//         if (iter->second == conn) {
//             return;
//         }
// 
//         iter->second->Destroy(true);
//         std::lock_guard<std::mutex> guard(erase_conns_mutex_);
//         erase_conns_.push_back(iter->second);
//         conn_map_.erase(iter);
//     }
// 
//     conn_map_[peer_spec] = conn;
// }

void TcpTransport::EraseConn(uint8_t thread_idx) {
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    // delay to release
    common::AutoSpinLock guard(erase_conns_mutex_);
    while (!erase_conns_.empty()) {
        auto from_item = erase_conns_.front();
        if (from_item->free_timeout_ms() <= now_tm_ms) {
            delete from_item;
            erase_conns_.pop_front();
            continue;
        }

        break;
    }

    erase_conn_tick_.CutOff(
        kEraseConnPeriod,
        std::bind(&TcpTransport::EraseConn, this, std::placeholders::_1));
}

std::string TcpTransport::GetHeaderHashForSign(const transport::protobuf::Header& message) {
    return common::Hash::keccak256("aa");
    assert(message.hash64());
    assert(message.hash64() != 0);
    std::string msg_for_hash;
    msg_for_hash.reserve(message.ByteSizeLong() + 64);
    msg_for_hash.append(message.des_dht_key());
    uint64_t hash64 = message.hash64();
    msg_for_hash.append(std::string((char*)&hash64, sizeof(hash64)));
    int32_t sharding_id = message.src_sharding_id();
    msg_for_hash.append(std::string((char*)&sharding_id, sizeof(sharding_id)));
    uint32_t type = message.type();
    msg_for_hash.append(std::string((char*)&type, sizeof(type)));
    int32_t version = message.version();
    msg_for_hash.append(std::string((char*)&version, sizeof(version)));
    protos::GetProtoHash(message, &msg_for_hash);
    return common::Hash::keccak256(msg_for_hash);
}

}  // namespace transport

}  // namespace zjchain
