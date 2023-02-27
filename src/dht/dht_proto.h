#pragma once

#include "common/utils.h"
#include "common/global_info.h"
#include "common/bloom_filter.h"
#include "dht/dht_utils.h"
#include "dht/base_dht.h"
#include "protos/dht.pb.h"
#include "protos/transport.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace dht {

class DhtProto {
public:
    static void CreateBootstrapRequest(
        int32_t local_sharding_id,
        const std::string& local_pubkey,
        const std::string& des_dht_key,
        transport::protobuf::Header& msg);
    static void CreateBootstrapResponse(
        int32_t local_sharding_id,
        const std::string& local_pubkey,
        const std::string& des_dht_key,
        const transport::MessagePtr& from_msg,
        transport::protobuf::Header& msg);
    static void CreateRefreshNeighborsRequest(
        const Dht& dht,
        const NodePtr& local_node,
        const std::string& des_dht_key,
        transport::protobuf::Header& msg);
    static void CreateRefreshNeighborsResponse(
        int32_t local_sharding_id,
        const std::string& des_dht_key,
        const std::vector<NodePtr>& nodes,
        transport::protobuf::Header& msg);
    static void CreateHeatbeatRequest(
        const NodePtr& local_node,
        const NodePtr& des_node,
        transport::protobuf::Header& msg);
    static void CreateHeatbeatResponse(
        const NodePtr& local_node,
        const std::string& des_dht_key,
        transport::protobuf::Header& msg);
    static int32_t CreateConnectRequest(
        bool response,
        const NodePtr& local_node,
        const std::string& des_dht_key,
        transport::protobuf::Header& msg);

private:
    DhtProto() {}
    ~DhtProto() {}

    DISALLOW_COPY_AND_ASSIGN(DhtProto);
};

}  // namespace dht

}  //namespace zjchain

