#include "network/network_proto.h"

#include "common/global_info.h"
#include "common/encode.h"
#include "common/hash.h"
//#include "ip/ip_utils.h"
//#include "security/ecdsa/security.h"
#include "transport/transport_utils.h"
#include "dht/dht_key.h"
#include "protos/network.pb.h"

namespace zjchain {

namespace network {

void NetworkProto::CreateGetNetworkNodesRequest(
        const dht::NodePtr& local_node,
        uint32_t network_id,
        uint32_t count,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    dht::DhtKeyManager dht_key(network_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kNetworkMessage);
    auto* net_msg = msg.mutable_network_proto();
    auto* get_nodes_req = net_msg->mutable_get_net_nodes_req();
    get_nodes_req->set_net_id(network_id);
    get_nodes_req->set_count(count);
}

void NetworkProto::CreateGetNetworkNodesResponse(
        const dht::NodePtr& local_node,
        const transport::protobuf::Header& header,
        const std::vector<dht::NodePtr>& nodes,
        transport::protobuf::Header& msg) {
    msg.set_src_sharding_id(local_node->sharding_id);
    dht::DhtKeyManager dht_key(header.src_sharding_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kNetworkMessage);
    auto* net_msg = msg.mutable_network_proto();
    auto* get_nodes_res = net_msg->mutable_get_net_nodes_res();
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]->pubkey_str.empty()) {
            continue;
        }

        auto proto_node = get_nodes_res->add_nodes();
        proto_node->set_sharding_id(nodes[i]->sharding_id);
        proto_node->set_public_ip(nodes[i]->public_ip);
        proto_node->set_public_port(nodes[i]->public_port);
        proto_node->set_pubkey(nodes[i]->pubkey_str);
    }
}

}  // namespace network

}  // namespace zjchain
