#pragma once

#include "protos/transport.pb.h"
#include "dht/dht_utils.h"

namespace zjchain {

namespace network {

class NetworkProto {
public:
    static void CreateGetNetworkNodesRequest(
            const dht::NodePtr& local_node,
            uint32_t network_id,
            uint32_t count,
            transport::protobuf::Header& msg);
    static void CreateGetNetworkNodesResponse(
            const dht::NodePtr& local_node,
            const transport::protobuf::Header& header,
            const std::vector<dht::NodePtr>& nodes,
            transport::protobuf::Header& msg);

private:
    NetworkProto() {}
    ~NetworkProto() {}

    DISALLOW_COPY_AND_ASSIGN(NetworkProto);
};

}  // namespace network

}  // namespace zjchain
