#pragma once

#include "common/bloom_filter.h"
#include "dht/dht_utils.h"
#include "protos/transport.pb.h"
#include "security/security.h"

namespace zjchain {

namespace elect {

class ElectProto {
public:
    static bool CreateElectWaitingNodes(
        std::shared_ptr<security::Security>& security_ptr,
        const dht::NodePtr& local_node,
        uint32_t waiting_shard_id,
        const std::string& balance_hash_256,
        const common::BloomFilter& nodes_filter,
        transport::protobuf::Header& msg);
    static bool CreateWaitingHeartbeat(
        std::shared_ptr<security::Security>& security_ptr,
        const dht::NodePtr& local_node,
        uint32_t waiting_shard_id,
        transport::protobuf::Header& msg);
    static bool CreateLeaderRotation(
        std::shared_ptr<security::Security>& security_ptr,
        const dht::NodePtr& local_node,
        const std::string& leader_id,
        uint32_t pool_mod_num,
        transport::protobuf::Header& msg);

private:
    ElectProto() {}
    ~ElectProto() {}

    DISALLOW_COPY_AND_ASSIGN(ElectProto);
};

}  // namespace elect

}  // namespace zjchain
