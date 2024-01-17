#include "dht/dht_proto.h"

#include "security/security.h"
#include "dht/dht_key.h"

namespace zjchain {

namespace dht {

void DhtProto::CreateBootstrapRequest(
        int32_t local_sharding_id,
        const std::string& local_pubkey,
        const std::string& des_dht_key,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_sharding_id);
    msg.set_des_dht_key(des_dht_key);
    msg.set_type(common::kDhtMessage);
    auto* dht_msg = msg.mutable_dht_proto();
    auto* bootstrap_req = dht_msg->mutable_bootstrap_req();
    bootstrap_req->set_pubkey(local_pubkey);
    bootstrap_req->set_public_ip(common::GlobalInfo::Instance()->config_local_ip());
    bootstrap_req->set_public_port(common::GlobalInfo::Instance()->config_local_port());
}

void DhtProto::CreateBootstrapResponse(
        int32_t local_sharding_id,
        const std::string& local_pubkey,
        const std::string& des_dht_key,
        const transport::MessagePtr& msg_ptr,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_sharding_id);
    msg.set_des_dht_key(des_dht_key);
    msg.set_type(common::kDhtMessage);
    // TODO(tt): add sign
    auto* res_dht_msg = msg.mutable_dht_proto();
    auto* bootstrap_res = res_dht_msg->mutable_bootstrap_res();
    bootstrap_res->set_public_ip(common::GlobalInfo::Instance()->config_local_ip());
    bootstrap_res->set_public_port(common::GlobalInfo::Instance()->config_local_port());
    bootstrap_res->set_pubkey(local_pubkey);
}

void DhtProto::CreateRefreshNeighborsRequest(
        const Dht& dht,
        const NodePtr& local_node,
        const std::string& des_dht_key,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    msg.set_des_dht_key(des_dht_key);
    msg.set_type(common::kDhtMessage);

    auto* dht_msg = msg.mutable_dht_proto();
    auto refresh_nei_req = dht_msg->mutable_refresh_neighbors_req();
    refresh_nei_req->set_count(kRefreshNeighborsDefaultCount);
    refresh_nei_req->set_pubkey(local_node->pubkey_str);
    refresh_nei_req->set_public_ip(common::GlobalInfo::Instance()->config_local_ip());
    refresh_nei_req->set_public_port(common::GlobalInfo::Instance()->config_local_port());
    common::BloomFilter bloomfilter{
            kRefreshNeighborsBloomfilterBitCount,
            kRefreshNeighborsBloomfilterHashCount };
    for (auto iter = dht.begin(); iter != dht.end(); ++iter) {
        ZJC_DEBUG("---1 hash: %lu id:%s shard:%u dht_key_hash:%lu", (*iter)->dht_key_hash, common::Encode::HexSubstr((*iter)->id).c_str(), (*iter)->sharding_id, (*iter)->dht_key_hash);
        // bloomfilter.Add(common::Hash::Hash64((*iter)->id));
        bloomfilter.Add((*iter)->dht_key_hash);
    }

    bloomfilter.Add(local_node->dht_key_hash);
    auto& bloomfilter_vec = bloomfilter.data();
    auto bloom_adder = refresh_nei_req->mutable_bloomfilter();
    for (uint32_t i = 0; i < bloomfilter_vec.size(); ++i) {
        bloom_adder->Add(bloomfilter_vec[i]);
    }
}

void DhtProto::CreateRefreshNeighborsResponse(
        int32_t local_sharding_id,
        const std::string& des_dht_key,
        const std::vector<NodePtr>& nodes,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_sharding_id);
    msg.set_des_dht_key(des_dht_key);
    msg.set_type(common::kDhtMessage);
    auto* dht_msg = msg.mutable_dht_proto();
    auto refresh_nei_res = dht_msg->mutable_refresh_neighbors_res();
    auto res_cnt = nodes.size();
    if (res_cnt > kRefreshNeighborsDefaultCount) {
        res_cnt = kRefreshNeighborsDefaultCount;
    }

    for (uint32_t i = 0; i < res_cnt; ++i) {
        auto proto_node = refresh_nei_res->add_nodes();
        proto_node->set_public_ip(nodes[i]->public_ip);
        proto_node->set_public_port(nodes[i]->public_port);
        proto_node->set_pubkey(nodes[i]->pubkey_str);
    }
}

void DhtProto::CreateHeatbeatRequest(
        const NodePtr& local_node,
        const NodePtr& des_node,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    msg.set_des_dht_key(des_node->dht_key);
    msg.set_type(common::kDhtMessage);
    auto* dht_msg = msg.mutable_dht_proto();
    auto heartbeat_req = dht_msg->mutable_heartbeat_req();
    heartbeat_req->set_dht_key_hash(local_node->dht_key_hash);
}

void DhtProto::CreateHeatbeatResponse(
        const NodePtr& local_node,
        const std::string& des_dht_key,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    msg.set_des_dht_key(des_dht_key);
    msg.set_type(common::kDhtMessage);
    auto* dht_msg = msg.mutable_dht_proto();
    auto heartbeat_res = dht_msg->mutable_heartbeat_res();
    heartbeat_res->set_dht_key_hash(local_node->dht_key_hash);
}

int32_t DhtProto::CreateConnectRequest(
        bool response,
        const NodePtr& local_node,
        const std::string& des_dht_key,
        transport::protobuf::Header& msg) {
    msg.set_des_dht_key(des_dht_key);
    msg.set_type(common::kDhtMessage);
    auto* dht_msg = msg.mutable_dht_proto();
    auto connect_req = dht_msg->mutable_connect_req();
    if (common::IsVlanIp(local_node->public_ip)) {
        return kDhtError;
    }

    connect_req->set_is_response(response);
    connect_req->set_pubkey(local_node->pubkey_str);
    connect_req->set_public_ip(common::GlobalInfo::Instance()->config_local_ip());
    connect_req->set_public_port(common::GlobalInfo::Instance()->config_local_port());
    return kDhtSuccess;
}

}  // namespace dht

}  //namespace zjchain

