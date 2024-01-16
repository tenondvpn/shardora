#include "dht/base_dht.h"

#include <stdio.h>

#include <bitset>
#include <algorithm>
#include <functional>
#include <limits>

#include "common/hash.h"
#include "common/encode.h"
#include "common/bloom_filter.h"
#include "common/country_code.h"
#include "common/time_utils.h"
#include "transport/processor.h"
#include "transport/transport_utils.h"
#include "transport/multi_thread.h"
#include "dht/dht_utils.h"
#include "dht/dht_proto.h"
#include "dht/dht_function.h"
#include "dht/dht_key.h"

namespace zjchain {

namespace dht {

BaseDht::BaseDht(NodePtr& local_node) : local_node_(local_node) {
    PrintDht(0);
}

BaseDht::~BaseDht() {}

int BaseDht::Init(
        std::shared_ptr<security::Security>& security,
        BootstrapResponseCallback boot_cb,
        NewNodeJoinCallback node_join_cb) {
    security_ = security;
    bootstrap_response_cb_ = boot_cb;
    node_join_cb_ = node_join_cb;
    if (local_node_->sharding_id == 0) {
        refresh_neighbors_tick_.CutOff(
            kRefreshNeighborPeriod,
            std::bind(&BaseDht::RefreshNeighbors, shared_from_this(), std::placeholders::_1));
    }

    auto tmp_dht_ptr = std::make_shared<Dht>(dht_);
    readonly_hash_sort_dht_ = tmp_dht_ptr;
    return kDhtSuccess;
}

int BaseDht::Destroy() {
    refresh_neighbors_tick_.Destroy();
    return kDhtSuccess;
}

void BaseDht::UniversalJoin(const NodePtr& node) {
    NodePtr new_node = std::make_shared<Node>(
        local_node_->sharding_id,
        node->public_ip,
        node->public_port,
        node->pubkey_str,
        node->id);
    new_node->join_way = kJoinFromUniversal;
    Join(new_node);
}

int BaseDht::Join(NodePtr& node) {
    DHT_DEBUG("sharding: %u, now try join new node: %s:%d",
        local_node_->sharding_id,
        node->public_ip.c_str(),
        node->public_port);

    if (node_join_cb_ != nullptr) {
        if (node_join_cb_(node) != kDhtSuccess) {
            DHT_DEBUG("check callback join node failed! %s, %d, sharding id: %d",
                common::Encode::HexEncode(node->id).c_str(), node->join_way, local_node_->sharding_id);
            return kDhtError;
        }
    }

    int res = CheckJoin(node);
    if (res != kDhtSuccess) {
        DHT_DEBUG("CheckJoin join node failed! %s, res: %d",
            common::Encode::HexEncode(node->id).c_str(), res);
        return res;
    }

    uint32_t b_dht_size = dht_.size();
    uint32_t b_map_size = node_map_.size();
    DhtFunction::PartialSort(local_node_->dht_key, dht_.size(), dht_);
    uint32_t replace_pos = dht_.size() + 1;
    if (!DhtFunction::Displacement(local_node_->dht_key, dht_, node, replace_pos)) {
        DHT_WARN("displacement for new node failed!");
        assert(false);
        return kDhtError;
    }

    if (replace_pos < dht_.size()) {
        auto rm_iter = dht_.begin() + replace_pos;
        auto hash_iter = node_map_.find((*rm_iter)->dht_key_hash);
        if (hash_iter != node_map_.end()) {
            node_map_.erase(hash_iter);
        }
        dht_.erase(rm_iter);
    }

    auto iter = node_map_.insert(std::make_pair(node->dht_key_hash, node));
    DHT_DEBUG("MMMMMMMM node_map_ size: %u", node_map_.size());
    if (!iter.second) {
        DHT_ERROR("kDhtNodeJoined join node failed! %s",
            common::Encode::HexEncode(node->id).c_str());
        return kDhtNodeJoined;
    }

    dht_.push_back(node);
    std::sort(
            dht_.begin(),
            dht_.end(),
            [](const NodePtr& lhs, const NodePtr& rhs)->bool {
        return lhs->id_hash < rhs->id_hash;
    });
        
    auto tmp_dht_ptr = std::make_shared<Dht>(dht_);
    readonly_hash_sort_dht_ = tmp_dht_ptr;
    DHT_DEBUG("sharding: %u, join new node: %s:%d",
        local_node_->sharding_id,
        node->public_ip.c_str(),
        node->public_port);
    valid_count_ = dht_.size() + 1;
    return kDhtSuccess;
}

int BaseDht::Drop(const std::string& id) {
    if (is_universal_) {
        return kDhtSuccess;
    }

    uint64_t dht_key_hash = 0;
    auto iter = std::find_if(
            dht_.begin(),
            dht_.end(),
            [id](const NodePtr& rhs) -> bool {
        return id == rhs->id;
    });
    if (iter == dht_.end()) {
        return kDhtSuccess;
    }

    dht_key_hash = (*iter)->dht_key_hash;
    DHT_DEBUG("success drop node: %s:%d", (*iter)->public_ip.c_str(), (*iter)->public_port);
    dht_.erase(iter);
    auto miter = node_map_.find(dht_key_hash);
    if (miter != node_map_.end()) {
        node_map_.erase(miter);
    }

    valid_count_ = dht_.size() + 1;
    return kDhtSuccess;
}

int BaseDht::Drop(const std::vector<std::string>& ids) {
    if (is_universal_) {
        return kDhtSuccess;
    }

    for (auto iter = ids.begin(); iter != ids.end(); ++iter) {
        Drop(*iter);
    }

    std::sort(
            dht_.begin(),
            dht_.end(),
            [](const NodePtr& lhs, const NodePtr& rhs)->bool {
        return lhs->id_hash < rhs->id_hash;
    });
    auto tmp_dht_ptr = std::make_shared<Dht>(dht_);
    readonly_hash_sort_dht_ = tmp_dht_ptr;
    return kDhtSuccess;
}

int BaseDht::Drop(NodePtr& node) {
    if (is_universal_) {
        return kDhtSuccess;
    }

    auto& dht_key_hash = node->dht_key_hash;
    auto iter = std::find_if(
            dht_.begin(),
            dht_.end(),
            [dht_key_hash](const NodePtr& rhs) -> bool {
        return dht_key_hash == rhs->dht_key_hash;
    });
    if (iter != dht_.end()) {
        assert((*iter)->id == node->id);
        dht_.erase(iter);
    }

    std::sort(
            dht_.begin(),
            dht_.end(),
            [](const NodePtr& lhs, const NodePtr& rhs)->bool {
        return lhs->id_hash < rhs->id_hash;
    });
    auto tmp_dht_ptr = std::make_shared<Dht>(dht_);
    readonly_hash_sort_dht_ = tmp_dht_ptr;
    auto miter = node_map_.find(node->dht_key_hash);
    if (miter != node_map_.end()) {
        assert(miter->second->id == node->id);
        node_map_.erase(miter);
    }

    DHT_DEBUG("success drop node: %s:%d", node->public_ip.c_str(), node->public_port);
    return kDhtSuccess;
}

int BaseDht::Drop(const std::string& ip, uint16_t port) {
    if (is_universal_) {
        return kDhtSuccess;
    }

    uint64_t dht_key_hash = 0;
    auto iter = std::find_if(
        dht_.begin(),
        dht_.end(),
        [ip, port](const NodePtr& rhs) -> bool {
            return ip == rhs->public_ip && port == rhs->public_port;
        });
    if (iter == dht_.end()) {
        return kDhtSuccess;
    }

    dht_key_hash = (*iter)->dht_key_hash;
    dht_.erase(iter);
    auto miter = node_map_.find(dht_key_hash);
    if (miter != node_map_.end()) {
        node_map_.erase(miter);
    }

    std::sort(
        dht_.begin(),
        dht_.end(),
        [](const NodePtr& lhs, const NodePtr& rhs)->bool {
            return lhs->id_hash < rhs->id_hash;
        });
    auto tmp_dht_ptr = std::make_shared<Dht>(dht_);
    readonly_hash_sort_dht_ = tmp_dht_ptr;
    DHT_DEBUG("success drop node: %s:%d", ip.c_str(), port);
    return kDhtSuccess;
}

int BaseDht::Bootstrap(
        uint8_t thread_idx,
        const std::vector<NodePtr>& boot_nodes,
        bool wait,
        int32_t sharding_id) {
    assert(!boot_nodes.empty());
    for (uint32_t i = 0; i < boot_nodes.size(); ++i) {
        if (boot_nodes[i]->public_ip == common::GlobalInfo::Instance()->config_local_ip() &&
            boot_nodes[i]->public_port == common::GlobalInfo::Instance()->config_local_port()) {
            continue;
        }

        // 构造一条 bootstrap message
        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto& msg = msg_ptr->header;
        DhtKeyManager dhtkey(local_node_->sharding_id, boot_nodes[i]->id);
        DhtProto::CreateBootstrapRequest(
            local_node_->sharding_id,
            security_->GetPublicKey(),
            dhtkey.StrKey(),
            msg);
        transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
        std::string sign;
        if (security_->Sign(
                transport::TcpTransport::Instance()->GetHeaderHashForSign(msg),
                &sign) != security::kSecuritySuccess) {
            continue;
        }

        msg.set_sign(sign);
        if (transport::TcpTransport::Instance()->Send(
                thread_idx,
                boot_nodes[i]->public_ip,
                boot_nodes[i]->public_port,
                msg) != transport::kTransportSuccess) {
            DHT_ERROR("bootstrap from %s:%d failed\n",
                boot_nodes[i]->public_ip.c_str(),
                boot_nodes[i]->public_port);
        } else {
            DHT_DEBUG("bootstrap from %s:%d success\n",
                boot_nodes[i]->public_ip.c_str(),
                boot_nodes[i]->public_port);
        }
    }

    return kDhtSuccess;
}

void BaseDht::SendToDesNetworkNodes(const transport::MessagePtr& msg_ptr) {
    auto& message = msg_ptr->header;
    uint32_t send_count = 0;
    auto dht_ptr = readonly_hash_sort_dht_;
    uint32_t des_net_id = DhtKeyManager::DhtKeyGetNetId(message.des_dht_key());
    for (auto iter = dht_ptr->begin(); iter != dht_ptr->end(); ++iter) {
        auto dht_node = (*iter);
        if (dht_node == nullptr) {
            continue;
        }

        uint32_t net_id = DhtKeyManager::DhtKeyGetNetId(dht_node->dht_key);
        if (net_id != des_net_id) {
            continue;
        }

        transport::TcpTransport::Instance()->Send(
            msg_ptr->thread_idx,
            dht_node->public_ip,
            dht_node->public_port,
            message);
        if (++send_count > 3) {
            break;
        }
    }

    if (send_count == 0) {
        SendToClosestNode(msg_ptr);
    }
}

void BaseDht::RandomSend(const transport::MessagePtr& msg_ptr) {
    auto& msg = msg_ptr->header;
    auto dhts = readonly_hash_sort_dht();
    if (dhts == nullptr || dhts->empty()) {
        return;
    }

    auto pos = rand() % dhts->size();
    transport::TcpTransport::Instance()->Send(
        msg_ptr->thread_idx,
        (*dhts)[pos]->public_ip,
        (*dhts)[pos]->public_port,
        msg);
}

void BaseDht::SendToClosestNode(const transport::MessagePtr& msg_ptr) {
    auto& message = msg_ptr->header;
    if (message.des_dht_key() == local_node_->dht_key) {
        DHT_ERROR("send to local dht key failed!");
        return;
    }

    auto dht_ptr = readonly_hash_sort_dht_;
    if (dht_ptr->empty()) {
        return;
    }

    auto closest_node = DhtFunction::GetClosestNode(*dht_ptr, message.des_dht_key());
    transport::TcpTransport::Instance()->Send(
        msg_ptr->thread_idx,
        closest_node->public_ip,
        closest_node->public_port,
        message);
    ZJC_DEBUG("send to closest node: %s:%u", closest_node->public_ip.c_str(), closest_node->public_port);
}

NodePtr BaseDht::FindNodeDirect(transport::protobuf::Header& message) {
    uint64_t des_dht_key_hash = common::Hash::Hash64(message.des_dht_key());
    std::shared_ptr<std::unordered_map<uint64_t, NodePtr>> readony_node_map;
    auto iter = readony_node_map->find(des_dht_key_hash);
    if (iter == readony_node_map->end()) {
        return nullptr;
    }

    return iter->second;
}

void BaseDht::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    if (header.type() != common::kDhtMessage) {
        //         DHT_ERROR("invalid message type[%d]", header.type());
        return;
    }

    if (!header.has_dht_proto()) {
        DHT_ERROR("protobuf::DhtMessage ParseFromString failed!");
        return;
    }

    DhtDispatchMessage(msg_ptr);
}

void BaseDht::DhtDispatchMessage(const transport::MessagePtr& msg_ptr) {
    if (msg_ptr->header.dht_proto().has_bootstrap_req()) {
//         ZJC_DEBUG("has_bootstrap_req");
        ProcessBootstrapRequest(msg_ptr);
        return;
    }

    if (msg_ptr->header.dht_proto().has_bootstrap_res()) {
//         ZJC_DEBUG("has_bootstrap_res");
        ProcessBootstrapResponse(msg_ptr);
        return;
    }

    if (msg_ptr->header.dht_proto().has_refresh_neighbors_req()) {
//         ZJC_DEBUG("has_refresh_neighbors_req");
        ProcessRefreshNeighborsRequest(msg_ptr);
        return;
    }

    if (msg_ptr->header.dht_proto().has_refresh_neighbors_res()) {
//         ZJC_DEBUG("has_refresh_neighbors_res");
        ProcessRefreshNeighborsResponse(msg_ptr);
        return;
    }

    if (msg_ptr->header.dht_proto().has_connect_req()) {
//         ZJC_DEBUG("has_connect_req");
        ProcessConnectRequest(msg_ptr);
        return;
    }
}

// 处理 bootstrap transportmessage
void BaseDht::ProcessBootstrapRequest(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& dht_msg = header.dht_proto();
    if (!dht_msg.has_bootstrap_req()) {
        DHT_WARN("dht message has no bootstrap request.");
        return;
    }

    // 验证消息签名
    std::string sign_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(header);
    if (security_->Verify(
            sign_hash,
            dht_msg.bootstrap_req().pubkey(),
            header.sign()) != security::kSecuritySuccess) {
        DHT_ERROR("verifi signature failed!");
        return;
    }

    auto id = security_->GetAddress(dht_msg.bootstrap_req().pubkey());
    DhtKeyManager dhtkey(header.src_sharding_id(), id);
    transport::protobuf::Header msg;
    DhtProto::CreateBootstrapResponse(
        local_node_->sharding_id,
        security_->GetPublicKey(),
        dhtkey.StrKey(),
        msg_ptr,
        msg);
    transport::TcpTransport::Instance()->SetMessageHash(msg, msg_ptr->thread_idx);
    std::string sign;
    if (security_->Sign(
            transport::TcpTransport::Instance()->GetHeaderHashForSign(msg),
            &sign) != security::kSecuritySuccess) {
        return;
    }

    msg.set_sign(sign);
    DHT_DEBUG("bootstrap response to: %s:%d, node: %s:%d",
        msg_ptr->conn->PeerIp().c_str(), msg_ptr->conn->PeerPort(),
        dht_msg.bootstrap_req().public_ip().c_str(),
        dht_msg.bootstrap_req().public_port());
    auto msg_str = msg.SerializeAsString();
    msg_ptr->conn->Send(msg_str);
    NodePtr node = std::make_shared<Node>(
        msg.src_sharding_id(),
        dht_msg.bootstrap_req().public_ip(),
        dht_msg.bootstrap_req().public_port(),
        dht_msg.bootstrap_req().pubkey(),
        security_->GetAddress(dht_msg.bootstrap_req().pubkey()));
    msg_ptr->conn->SetPeerIp(dht_msg.bootstrap_req().public_ip());
    msg_ptr->conn->SetPeerPort(dht_msg.bootstrap_req().public_port());
    node->join_way = kJoinFromBootstrapReq;
    Join(node);
}

void BaseDht::ProcessBootstrapResponse(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto& dht_msg = header.dht_proto();
    ZJC_DEBUG("boot response coming.");
    if (!CheckDestination(header.des_dht_key(), false)) {
        DHT_WARN("bootstrap request destination error[%s][%s]!",
            common::Encode::HexEncode(header.des_dht_key()).c_str(),
            common::Encode::HexEncode(local_node_->dht_key).c_str());
        return;
    }

    if (!dht_msg.has_bootstrap_res()) {
        return;
    }

    if (!dht_msg.bootstrap_res().has_pubkey() || dht_msg.bootstrap_res().pubkey().empty()) {
        return;
    }

    // check sign
    std::string sign_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(header);
    if (security_->Verify(
            sign_hash,
            dht_msg.bootstrap_res().pubkey(),
            header.sign()) != security::kSecuritySuccess) {
        DHT_ERROR("verifi signature failed!");
        return;
    }

    NodePtr node = std::make_shared<Node>(
        header.src_sharding_id(),
        dht_msg.bootstrap_res().public_ip(),
        dht_msg.bootstrap_res().public_port(),
        dht_msg.bootstrap_res().pubkey(),
        security_->GetAddress(dht_msg.bootstrap_res().pubkey()));
    node->join_way = kJoinFromBootstrapRes;
    msg_ptr->conn->SetPeerIp(dht_msg.bootstrap_res().public_ip());
    msg_ptr->conn->SetPeerPort(dht_msg.bootstrap_res().public_port());
    Join(node);
    if (joined_) {
        return;
    }

    joined_ = true;
    DHT_DEBUG("join success!");
    if (bootstrap_response_cb_ != nullptr) {
        // set global country
        bootstrap_response_cb_(this, dht_msg);
    }
}

void BaseDht::ProcessRefreshNeighborsRequest(const transport::MessagePtr& msg_ptr) {
    if (!is_universal_) {
        return;
    }

    auto& header = msg_ptr->header;
    auto& dht_msg = header.dht_proto();
    if (!dht_msg.has_refresh_neighbors_req()) {
        DHT_WARN("not refresh neighbor request.");
        return;
    }

    std::string sign_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(header);
    if (security_->Verify(
            sign_hash,
            dht_msg.refresh_neighbors_req().pubkey(),
            header.sign()) != security::kSecuritySuccess) {
        DHT_ERROR("verifi signature failed!");
        return;
    }

    NodePtr node = std::make_shared<Node>(
        header.src_sharding_id(),
        dht_msg.refresh_neighbors_req().public_ip(),
        dht_msg.refresh_neighbors_req().public_port(),
        dht_msg.refresh_neighbors_req().pubkey(),
        security_->GetAddress(dht_msg.refresh_neighbors_req().pubkey()));
    msg_ptr->conn->SetPeerIp(dht_msg.refresh_neighbors_req().public_ip());
    msg_ptr->conn->SetPeerPort(dht_msg.refresh_neighbors_req().public_port());
    node->join_way = kJoinFromRefreshNeigberRequest;
    Join(node);
    std::vector<uint64_t> bloomfilter_vec;
    for (auto i = 0; i < dht_msg.refresh_neighbors_req().bloomfilter_size(); ++i) {
        bloomfilter_vec.push_back(dht_msg.refresh_neighbors_req().bloomfilter(i));
    }

    std::shared_ptr<common::BloomFilter> bloomfilter{ nullptr };
    if (!bloomfilter_vec.empty()) {
        bloomfilter = std::make_shared<common::BloomFilter>(
                bloomfilter_vec,
                kRefreshNeighborsBloomfilterHashCount);
    }

    Dht tmp_dht;
    if (bloomfilter) {
        auto& closest_nodes = dht_;
        for (auto iter = closest_nodes.begin(); iter != closest_nodes.end(); ++iter) {
            ZJC_DEBUG("---2 port:%u, src_shardid: ,hash: %lu id:%s shard:%u", dht_msg.refresh_neighbors_req().public_port(), header.src_sharding_id(), (*iter)->dht_key_hash, common::Encode::HexSubstr((*iter)->id).c_str(), (*iter)->sharding_id);
            if (bloomfilter->Contain((*iter)->dht_key_hash)) {
                ZJC_DEBUG("res refresh neighbers filter: %s:%u, hash: %lu",
                    common::Encode::HexEncode((*iter)->dht_key).c_str(), msg_ptr->header.hash64());
                continue;
            }

            ZJC_DEBUG("res refresh neighbers new node: %s:%u, hash: %lu",
                (*iter)->public_ip.c_str(), (*iter)->public_port, msg_ptr->header.hash64());
            tmp_dht.push_back((*iter));
        }

        if (!bloomfilter->Contain(local_node_->dht_key_hash)) {
            tmp_dht.push_back(local_node_);
        }
    }

    auto id = security_->GetAddress(dht_msg.refresh_neighbors_req().pubkey());
    DhtKeyManager dhtkey(header.src_sharding_id(), id);
    auto close_nodes = DhtFunction::GetClosestNodes(
        tmp_dht,
        dhtkey.StrKey(),
        kRefreshNeighborsDefaultCount + 1);
    if (close_nodes.empty()) {
        ZJC_DEBUG("res refresh neighbers filter empty %lu", msg_ptr->header.hash64());
        return;
    }

    transport::protobuf::Header res;
    DhtProto::CreateRefreshNeighborsResponse(
        local_node_->sharding_id,
        local_node_->dht_key,
        close_nodes,
        res);
    transport::TcpTransport::Instance()->SetMessageHash(res, msg_ptr->thread_idx);
    ZJC_DEBUG("send refresh neighbers response hash: %lu", res.hash64());
    msg_ptr->conn->Send(res.SerializeAsString());
}

void BaseDht::ProcessRefreshNeighborsResponse(const transport::MessagePtr& msg_ptr) {
    if (!is_universal_) {
        return;
    }

    auto& header = msg_ptr->header;
    auto& dht_msg = header.dht_proto();
    ZJC_DEBUG("receive refresh neighbers response hash: %lu, size: %u",
        msg_ptr->header.hash64(),
        dht_msg.refresh_neighbors_res().nodes_size());
    if (!dht_msg.has_refresh_neighbors_res()) {
        return;
    }

    const auto& res_nodes = dht_msg.refresh_neighbors_res().nodes();
    for (int32_t i = 0; i < res_nodes.size(); ++i) {
        ZJC_DEBUG("connect neighbers new node: %s:%u",
            res_nodes[i].public_ip().c_str(), res_nodes[i].public_port());
        Connect(
            msg_ptr->thread_idx,
            res_nodes[i].public_ip(),
            res_nodes[i].public_port(),
            res_nodes[i].pubkey(),
            header.src_sharding_id(),
            false);
    }
}

void BaseDht::Connect(
        uint8_t thread_idx,
        const std::string& des_ip,
        uint16_t des_port,
        const std::string& des_pubkey,
        int32_t src_sharding_id,
        bool response) {
    if (des_ip == "0.0.0.0" || des_port == 0) {
        ZJC_DEBUG("des_ip == 0.0.0.0 || des_port == 0");
        return;
    }

    if (des_ip == local_node_->public_ip && des_port == local_node_->public_port) {
        return;
    }

    auto peer_int = common::GetNodeConnectInt(des_ip, des_port);
    auto now_tm_ms = common::TimeUtils::TimestampMs();
    auto iter = connect_timeout_map_.find(peer_int);
    if (iter != connect_timeout_map_.end()) {
        if (iter->second >= now_tm_ms) {
            ZJC_DEBUG("iter->second >= now_tm_ms: %lu, %lu", iter->second, now_tm_ms);
            return;
        }
    }

    if (connect_timeout_map_.size() >= 102400) {
        connect_timeout_map_.clear();
    }

    connect_timeout_map_[peer_int] = now_tm_ms + kConnectTimeoutMs;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    auto id = security_->GetAddress(des_pubkey);
    DhtKeyManager dhtkey(src_sharding_id, id);
    if (DhtProto::CreateConnectRequest(
            response,
            local_node_,
            dhtkey.StrKey(),
            msg) == kDhtSuccess) {
        std::string sign;
        transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
        if (security_->Sign(
                transport::TcpTransport::Instance()->GetHeaderHashForSign(msg),
                &sign) != security::kSecuritySuccess) {
            ZJC_DEBUG("sign error");
            return;
        }

        msg.set_sign(sign);
        transport::TcpTransport::Instance()->Send(
            thread_idx,
            des_ip,
            des_port,
            msg);
        DHT_DEBUG("connect to: %s:%d, %lu, %lu, %lu, hash: %lu", des_ip.c_str(),
            des_port, peer_int, connect_timeout_map_[peer_int], now_tm_ms, msg.hash64());
    }
}

void BaseDht::ProcessConnectRequest(const transport::MessagePtr& msg_ptr) {
    if (!is_universal_) {
        ZJC_DEBUG("not universal");
        return;
    }

    auto& header = msg_ptr->header;
    auto& dht_msg = header.dht_proto();
    if (header.des_dht_key() != local_node_->dht_key) {
        ZJC_DEBUG("header.des_dht_key() != local_node_->dht_key : %lu",
            msg_ptr->header.hash64());
        return;
    }

    if (!dht_msg.has_connect_req()) {
        ZJC_DEBUG("!dht_msg.has_connect_req(): %lu", msg_ptr->header.hash64());
        return;
    }

    std::string sign_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(header);
    if (security_->Verify(
            sign_hash,
            dht_msg.connect_req().pubkey(),
            header.sign()) != security::kSecuritySuccess) {
        DHT_ERROR("verifi signature failed: %lu", msg_ptr->header.hash64());
        return;
    }

    NodePtr node = std::make_shared<Node>(
        header.src_sharding_id(),
        dht_msg.connect_req().public_ip(),
        dht_msg.connect_req().public_port(),
        dht_msg.connect_req().pubkey(),
        security_->GetAddress(dht_msg.connect_req().pubkey()));
    node->join_way = kJoinFromConnect;
    msg_ptr->conn->SetPeerIp(dht_msg.connect_req().public_ip());
    msg_ptr->conn->SetPeerPort(dht_msg.connect_req().public_port());
    Join(node);
    if (dht_msg.connect_req().is_response()) {
        DHT_ERROR("process connect response success: %lu", msg_ptr->header.hash64());
        return;
    }

    Connect(
        msg_ptr->thread_idx,
        dht_msg.connect_req().public_ip(),
        dht_msg.connect_req().public_port(),
        dht_msg.connect_req().pubkey(),
        header.src_sharding_id(),
        true);
    DHT_ERROR("process connect success: %lu", msg_ptr->header.hash64());
}

bool BaseDht::NodeValid(NodePtr& node) {
    if (node->dht_key.size() != kDhtKeySize) {
        DHT_ERROR("dht key size must[%u] now[%u]", kDhtKeySize, node->dht_key.size());
        return false;
    }

    if (node->public_ip.empty() || node->public_port <= 0) {
        DHT_ERROR("node[%s] public ip or port invalid!",
                common::Encode::HexEncode(node->id).c_str());
        return false;
    }

    if (node->public_ip == local_node_->public_ip &&
            node->public_port == local_node_->public_port) {
        return false;
    }

    return true;
}

bool BaseDht::NodeJoined(NodePtr& node) {
    auto iter = node_map_.find(node->dht_key_hash);
    if (iter != node_map_.end()) {
        return true;
    }

    return false;
}

int BaseDht::CheckJoin(NodePtr& node) {
    if (!is_universal_) {
        if (node->sharding_id != local_node_->sharding_id) {
            return kDhtInvalidNat;
        }
    }

    if (node->public_ip == "0.0.0.0" || common::IsVlanIp(node->public_ip)) {
        ZJC_DEBUG("ip invalid: %s, is vlan ip: %d",
            node->public_ip.c_str(), common::IsVlanIp(node->public_ip));
        return kDhtIpInvalid;
    }

    if (node->pubkey_str.empty() || node->id.empty() || node->dht_key.empty()) {
        DHT_ERROR("invalid node nat type pubkey or id or dht key is empty.[%d][%d][%d][%d][%s:%d]",
                node->pubkey_str.empty(),
                node->id.empty(),
                node->dht_key.empty(),
                dht::DhtKeyManager::DhtKeyGetNetId(node->dht_key),
                node->public_ip.c_str(),
                node->public_port);
        return kDhtKeyInvalid;
    }

    if (!NodeValid(node)) {
        return kNodeInvalid;
    }

    if (node->dht_key_hash == 0) {
        return kDhtKeyHashError;
    }

    if (node->dht_key_hash == local_node_->dht_key_hash) {
//         DHT_ERROR("self join[%s][%s][%llu][%llu]",
//                 common::Encode::HexEncode(node->dht_key).c_str(),
//                 common::Encode::HexEncode(local_node_->dht_key).c_str(),
//                 node->dht_key_hash,
//                 local_node_->dht_key_hash);
        return kDhtKeyHashError;
    }

    if (node->dht_key == local_node_->dht_key) {
        return kDhtKeyHashError;
    }

    if (NodeJoined(node)) {
        return kDhtNodeJoined;
    }

    if (DhtFunction::GetDhtBucket(local_node_->dht_key, node) != kDhtSuccess) {
        DHT_ERROR("compute node dht bucket index failed!");
        return kDhtGetBucketError;
    }

    if (dht_.size() >= kDhtMaxNeighbors) {
        DhtFunction::PartialSort(local_node_->dht_key, dht_.size(), dht_);
        uint32_t replace_pos = dht_.size() + 1;
        if (!DhtFunction::Displacement(local_node_->dht_key, dht_, node, replace_pos)) {
//             DHT_ERROR("Displacement failed[%s]",
//                     common::Encode::HexEncode(node->id).c_str());
            return kDhtMaxNeiborsError;
        }
    }
    return kDhtSuccess;
}

bool BaseDht::CheckDestination(const std::string& des_dht_key, bool check_closest) {
    if (des_dht_key == local_node_->dht_key) {
        return true;
    }

    if (!check_closest) {
        return false;
    }

    bool closest = false;
    if (DhtFunction::IsClosest(
            des_dht_key,
            local_node_->dht_key,
            dht_,
            closest) != kDhtSuccess) {
        return false;
    }

    return closest;
}

void BaseDht::RefreshNeighbors(uint8_t thread_idx) {
    if (!is_universal_) {
        return;
    }

//     auto dht_ptr = readonly_hash_sort_dht_;
//     if (!dht_ptr->empty()) {
//         auto rand_idx = common::Random::RandomInt32() % dht_ptr->size();
//         auto node = (*dht_ptr)[rand_idx];
//         transport::protobuf::Header msg;
//         msg.set_src_sharding_id(local_node_->sharding_id);
//         dht::DhtKeyManager dht_key(local_node_->sharding_id);
//         msg.set_des_dht_key(dht_key.StrKey());
//         msg.set_type(common::kDhtMessage);
//         auto* dht_msg = msg.mutable_dht_proto();
//         auto timer_req = dht_msg->mutable_timer();
//         timer_req->set_tm_milli(common::TimeUtils::TimestampMs());
//         transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
//         transport::TcpTransport::Instance()->Send(
//             thread_idx,
//             node->public_ip,
//             node->public_port,
//             msg);
//         ZJC_DEBUG("refresh neighbors now %s:%d! hash: %lu",
//             node->public_ip.c_str(), node->public_port, msg.hash64());
//     }
    ProcessTimerRequest(thread_idx);
    refresh_neighbors_tick_.CutOff(
        kRefreshNeighborPeriod,
        std::bind(&BaseDht::RefreshNeighbors, shared_from_this(), std::placeholders::_1));
}

void BaseDht::ProcessTimerRequest(uint8_t thread_idx) {
//     auto now_tm = common::TimeUtils::TimestampUs();
//     if (now_tm < prev_refresh_neighbor_tm_ + kRefreshNeighborPeriod) {
//         return;
//     }
// 
//     prev_refresh_neighbor_tm_ = now_tm;
    auto dht_ptr = readonly_hash_sort_dht_;
    if (dht_ptr == nullptr || dht_ptr->empty()) {
        return;
    }

    auto rand_idx = common::Random::RandomInt32() % dht_ptr->size();
    auto node = (*dht_ptr)[rand_idx];
    transport::protobuf::Header msg;
    DhtProto::CreateRefreshNeighborsRequest(
        *dht_ptr,
        local_node_,
        node->dht_key,
        msg);
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    std::string sign;
    if (security_->Sign(
            transport::TcpTransport::Instance()->GetHeaderHashForSign(msg),
            &sign) != security::kSecuritySuccess) {
        return;
    }

    msg.set_sign(sign);
    transport::TcpTransport::Instance()->Send(
        thread_idx,
        node->public_ip,
        node->public_port,
        msg);
    ZJC_DEBUG("refresh neighbors now %s:%d! hash: %lu",
        node->public_ip.c_str(), node->public_port, msg.hash64());
}

void BaseDht::PrintDht(uint8_t thread_idx) {
    dht::DhtPtr readonly_dht = readonly_hash_sort_dht();
    if (readonly_dht != nullptr) {
        auto node = local_node();
        std::string debug_str;
        std::string res = common::StringUtil::Format(
            "dht num: %d, local: %s, id: %u, %s, public port: %u",
            (readonly_dht->size() + 1),
            common::Encode::HexEncode(node->id).c_str(),
            local_node()->sharding_id,
            node->public_ip.c_str(),
            node->public_port);
        for (auto iter = readonly_dht->begin(); iter != readonly_dht->end(); ++iter) {
            auto node = *iter;
            assert(node != nullptr);
            std::string tmp_res = common::StringUtil::Format(
                "\n%s, id: %u, %s:%u",
                common::Encode::HexSubstr(node->id).c_str(),
                node->sharding_id,
                node->public_ip.c_str(),
                node->public_port);
            res += tmp_res;
        }

        ZJC_DEBUG("dht info sharding_id: %u, %s", local_node()->sharding_id, res.c_str());
    }
   
    tick_.CutOff(10000000lu, std::bind(&BaseDht::PrintDht, this, std::placeholders::_1));
}

}  // namespace dht

}  // namespace zjchain
