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

    msg_random_ = common::Random::RandomString(32);
}

TcpTransport::~TcpTransport() {}

int TcpTransport::Init(
        const std::string& ip_port,
        int backlog,
        bool create_server,
        MultiThreadHandler* msg_handler) {
    // server just one thread
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

    output_thread_ = std::make_shared<std::thread>(&TcpTransport::Output, this);
    return kTransportSuccess;
}

void TcpTransport::Stop() {
    destroy_ = true;
    if (acceptor_ != nullptr) {
        acceptor_->Stop();
        acceptor_->Destroy();
    }

    if (transport_) {
        transport_->Stop();
        transport_->Destroy();
    }

    if (output_thread_ != nullptr) {
        output_thread_->join();
    }
}

bool TcpTransport::OnClientPacket(tnet::TcpConnection* conn, tnet::Packet& packet) {
    ZJC_DEBUG("message coming");
    auto tcp_conn = dynamic_cast<tnet::TcpConnection*>(conn);
    if (conn->GetSocket() == nullptr) {
        packet.Free();
        ZJC_DEBUG("message coming failed 0");
        return false;
    }

    std::string from_ip;
    uint16_t from_port;
    conn->GetSocket()->GetIpPort(&from_ip, &from_port);
    if (packet.IsCmdPacket()) {
        if (packet.PacketType() == tnet::CmdPacket::CT_TCP_NEW_CONNECTION) {
            // add connection
            packet.Free();
            ZJC_DEBUG("message coming failed 1");
            return true;
        }

        if (conn->is_client()) {
            conn->Destroy(true);
        }
        
//         if (!conn->PeerIp().empty() && conn->PeerPort() != 0) {
//             CreateDropNodeMessage(conn->PeerIp(), conn->PeerPort());
//         }

        packet.Free();
        ZJC_DEBUG("message coming failed 2 type: %d", packet.PacketType());
        return false;
    }

    // network message must free memory
    tnet::MsgPacket* msg_packet = dynamic_cast<tnet::MsgPacket*>(&packet);
    char* data = nullptr;
    uint32_t len = 0;
    msg_packet->GetMessageEx(&data, &len);
    if (len >= kTcpBuffLength) {
        ZJC_DEBUG("message coming failed 3");
        return false;
    }

    MessagePtr msg_ptr = std::make_shared<TransportMessage>();
    if (!msg_ptr->header.ParseFromArray(data, len)) {
        TRANSPORT_ERROR("Message ParseFromString from string failed!"
            "[%s:%d][len: %d]",
            from_ip.c_str(), from_port, len);
        ZJC_DEBUG("message coming failed 4");
        return false;
    }

    if (msg_ptr->header.has_from_public_port()) {
        from_port = msg_ptr->header.from_public_port();
    }

    conn->SetPeerIp(from_ip);
    conn->SetPeerPort(from_port);
    ZJC_DEBUG("message coming: %s:%d", from_ip.c_str(), from_port);
    msg_ptr->conn = conn;
    msg_handler_->HandleMessage(msg_ptr);
    if (!conn->is_client() && added_conns_.Push(conn)) {
        from_client_conn_queues_.push(conn);
    }

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
    std::string hash_str;
    hash_str.reserve(1024);
    hash_str.append(msg_random_);
    hash_str.append((char*)&thread_idx, sizeof(thread_idx));
    auto msg_count = ++thread_msg_count_[thread_idx];
    hash_str.append((char*)&msg_count, sizeof(msg_count));
    tmpHeader->set_hash64(common::Hash::Hash64(hash_str));
}

int TcpTransport::Send(
        uint8_t thread_idx,
        tnet::TcpInterface* tcp_conn,
        const transport::protobuf::Header& message) {
    assert(message.broadcast().bloomfilter_size() < 64);
    auto tmpHeader = const_cast<transport::protobuf::Header*>(&message);
    tmpHeader->set_from_public_port(common::GlobalInfo::Instance()->config_public_port());
    std::string msg;
    if (!message.has_hash64() || message.hash64() == 0) {
        SetMessageHash(message, thread_idx);
    }

    ZJC_DEBUG("send message hash64: %lu", message.hash64());
    message.SerializeToString(&msg);
    if (tcp_conn->Send(msg) != 0) {
        auto* tmp_conn = static_cast<tnet::TcpConnection*>(tcp_conn);
        assert(tmp_conn != nullptr);
        if (tmp_conn->is_client()) {
            tmp_conn->Destroy(true);
        }

        return kTransportError;
    }

    return kTransportSuccess;
}

int TcpTransport::Send(
        uint8_t thread_idx,
        const std::string& des_ip,
        uint16_t des_port,
        const transport::protobuf::Header& message) {
    assert(thread_idx < common::kMaxThreadCount);
    auto tmpHeader = const_cast<transport::protobuf::Header*>(&message);
    tmpHeader->set_from_public_port(common::GlobalInfo::Instance()->config_public_port());
    assert(message.broadcast().bloomfilter_size() < 64);
    if (!message.has_hash64() || message.hash64() == 0) {
        SetMessageHash(message, thread_idx);
    }

    auto output_item = std::make_shared<ClientItem>();
    output_item->des_ip = des_ip;
    output_item->port = des_port;
    message.SerializeToString(&output_item->msg);
    output_queues_[thread_idx].push(output_item);
    output_con_.notify_one();
    return kTransportSuccess;
}


void TcpTransport::EraseConn(uint64_t now_tm_ms) {
    // delay to release
    while (!erase_conns_.empty()) {
        auto from_item = erase_conns_.front();
        if (from_item->free_timeout_ms() <= now_tm_ms) {
            if (!from_item->is_client()) {
                std::string key = from_item->PeerIp() + ":" + std::to_string(from_item->PeerPort());
                auto iter = from_conn_map_.find(key);
                if (iter != from_conn_map_.end()) {
                    from_conn_map_.erase(iter);
                }
            }

            delete from_item;
            erase_conns_.pop_front();
            continue;
        }

        break;
    }
}

void TcpTransport::Output() {
    while (!destroy_) {
        auto now_tm_ms = common::TimeUtils::TimestampMs();
        if (prev_erase_timestamp_ms_ < now_tm_ms) {
            EraseConn(now_tm_ms);
            prev_erase_timestamp_ms_ = now_tm_ms + kCheckEraseConnPeriodMs;
        }

        while (from_client_conn_queues_.size() > 0) {
            tnet::TcpConnection* conn = nullptr;
            from_client_conn_queues_.pop(&conn);
            std::string key = conn->PeerIp() + ":" + std::to_string(conn->PeerPort());
            from_conn_map_[key] = conn;
        }

        for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
            while (output_queues_[i].size() > 0) {
                std::shared_ptr<ClientItem> item_ptr = nullptr;
                output_queues_[i].pop(&item_ptr);
                auto tcp_conn = GetConnection(item_ptr->des_ip, item_ptr->port);
                if (tcp_conn == nullptr) {
                    TRANSPORT_ERROR("get tcp connection failed[%s][%d][hash64: %llu]",
                        item_ptr->des_ip.c_str(), item_ptr->port, 0);
                    continue;
                }

                if (tcp_conn->Send(item_ptr->msg) != 0) {
                    TRANSPORT_ERROR("send to tcp connection failed[%s][%d][hash64: %llu]",
                        item_ptr->des_ip.c_str(), item_ptr->port, 0);
                    tcp_conn->Destroy(true);
                    continue;
                }

                ZJC_DEBUG("send message %s:%u, hash64: %lu, size: %u",
                    item_ptr->des_ip.c_str(), item_ptr->port, 0, item_ptr->msg.size());
            }
        }

        std::unique_lock<std::mutex> lock(output_mutex_);
        output_con_.wait_for(lock, std::chrono::milliseconds(10));
    }
}

int TcpTransport::GetSocket() {
    return socket_->GetFd();
}

// tnet::TcpConnection* TcpTransport::CreateConnection(const std::string& ip, uint16_t port) {
//     if (ip == "0.0.0.0") {
//         return nullptr;
//     }
// 
//     std::string peer_spec = ip + ":" + std::to_string(port);
// //     std::string local_spec = common::GlobalInfo::Instance()->config_local_ip() + ":" +
// //         std::to_string(common::GlobalInfo::Instance()->config_local_port());
//     return transport_->CreateConnection(
//             peer_spec,
//             "",
//             300u * 1000u * 1000u);
// }

tnet::TcpConnection* TcpTransport::GetConnection(
        const std::string& ip,
        uint16_t port) {
    if (ip == "0.0.0.0" || port == 0) {
        return nullptr;
    }

    std::string peer_spec = ip + ":" + std::to_string(port);
    auto from_iter = from_conn_map_.find(peer_spec);
    if (from_iter != from_conn_map_.end()) {
        if (!from_iter->second->ShouldReconnect()) {
            ZJC_DEBUG("use exists client connect send message %s:%d", ip.c_str(), port);
            return from_iter->second;
        }

        erase_conns_.push_back(from_iter->second);
        from_conn_map_.erase(from_iter);
    }

    auto iter = conn_map_.find(peer_spec);
    if (iter != conn_map_.end()) {
        if (iter->second->ShouldReconnect()) {
            erase_conns_.push_back(iter->second);
            ZJC_DEBUG("remove connect and reconnect send message %s:%d", ip.c_str(), port);
            conn_map_.erase(iter);
        } else {
            ZJC_DEBUG("use exists connect send message %s:%d", ip.c_str(), port);
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
    
    tcp_conn->set_client();
    ZJC_DEBUG("success connect send message %s:%d", ip.c_str(), port);
    conn_map_[peer_spec] = tcp_conn;
    return tcp_conn;
}

std::string TcpTransport::GetHeaderHashForSign(const transport::protobuf::Header& message) {
    assert(message.has_hash64());
    assert(message.hash64() != 0);
    std::string msg_for_hash;
    msg_for_hash.reserve(3 * 1024 * 1024);
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
