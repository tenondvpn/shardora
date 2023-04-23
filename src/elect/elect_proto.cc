#include "elect/elect_proto.h"

#include <limits>

#include "common/country_code.h"
#include "common/global_info.h"
#include "common/time_utils.h"
#include "dht/dht_key.h"
#include "dht/base_dht.h"
#include "elect/elect_utils.h"
#include "network/network_utils.h"
#include "network/dht_manager.h"
#include "protos/elect.pb.h"
#include "security/security.h"
#include "transport/tcp_transport.h"

namespace zjchain {

namespace elect {

bool ElectProto::CreateLeaderRotation(
        std::shared_ptr<security::Security>& security_ptr,
        const dht::NodePtr& local_node,
        const std::string& leader_id,
        uint32_t pool_mod_num,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    dht::DhtKeyManager dht_key(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kElectMessage);
    // now just for test
    protobuf::ElectMessage ec_msg;
    auto leader_rotation = ec_msg.mutable_leader_rotation();
    leader_rotation->set_leader_id(leader_id);
    leader_rotation->set_pool_mod_num(pool_mod_num);
    ec_msg.set_pubkey(security_ptr->GetPublicKey());
    auto broad_param = msg.mutable_broadcast();
    broad_param->set_hop_limit(10);
    std::string hash_str = leader_id + std::to_string(pool_mod_num);
    auto message_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    std::string sign;
    int sign_res = security_ptr->Sign(message_hash, &sign);
    if (sign_res != security::kSecuritySuccess) {
        ELECT_ERROR("signature error.");
        return false;
    }

    msg.set_sign(sign);
    return true;
}

bool ElectProto::CreateElectWaitingNodes(
        std::shared_ptr<security::Security>& security_ptr,
        const dht::NodePtr& local_node,
        uint32_t waiting_shard_id,
        const std::string& balance_hash_256,
        const common::BloomFilter& nodes_filter,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kElectMessage);
    protobuf::ElectMessage ec_msg;
    auto waiting_nodes_msg = ec_msg.mutable_waiting_nodes();
    for (uint32_t i = 0; i < nodes_filter.data().size(); ++i) {
        waiting_nodes_msg->add_nodes_filter(nodes_filter.data()[i]);
    }

    waiting_nodes_msg->set_stoke_hash(balance_hash_256);
    std::string hash_str = nodes_filter.Serialize() +
        std::to_string(waiting_shard_id) +
        balance_hash_256;
    waiting_nodes_msg->set_waiting_shard_id(waiting_shard_id);
    ec_msg.set_pubkey(security_ptr->GetPublicKey());
    auto broad_param = msg.mutable_broadcast();
    broad_param->set_hop_limit(10);
    auto message_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    std::string sign;
    int sign_res = security_ptr->Sign(message_hash, &sign);
    if (sign_res != security::kSecuritySuccess) {
        ELECT_ERROR("signature error.");
        return false;
    }

    msg.set_sign(sign);
    return true;
}

bool ElectProto::CreateWaitingHeartbeat(
        std::shared_ptr<security::Security>& security_ptr,
        const dht::NodePtr& local_node,
        uint32_t waiting_shard_id,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kElectMessage);
    protobuf::ElectMessage ec_msg;
    auto heartbeat_msg = ec_msg.mutable_waiting_heartbeat();
    heartbeat_msg->set_public_ip(local_node->public_ip);
    heartbeat_msg->set_public_port(local_node->public_port);
    heartbeat_msg->set_network_id(waiting_shard_id);
    heartbeat_msg->set_timestamp_sec(common::TimeUtils::TimestampSeconds());
    ec_msg.set_pubkey(security_ptr->GetPublicKey());
    auto broad_param = msg.mutable_broadcast();
    broad_param->set_hop_limit(10);
    auto message_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg);
    std::string sign;
    int sign_res = security_ptr->Sign(message_hash, &sign);
    if (sign_res != security::kSecuritySuccess) {
        ELECT_ERROR("signature error.");
        return false;
    }

    msg.set_sign(sign);
    return true;
}

}  // namespace elect

}  // namespace zjchain
