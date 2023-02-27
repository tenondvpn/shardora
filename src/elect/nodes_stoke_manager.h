#pragma once

#include "elect/elect_utils.h"
#include "protos/elect.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace elect {

class NodesStokeManager {
public:
    NodesStokeManager(std::shared_ptr<security::Security>& security_ptr)
        : security_ptr_(security_ptr) {}
    ~NodesStokeManager() {}
    void SyncAddressStoke(const std::vector<std::string>& addrs);
    uint64_t GetAddressStoke(const std::string& addr);
    void HandleSyncAddressStoke(
        const transport::protobuf::Header& header,
        const protobuf::ElectMessage& ec_msg);
    void HandleSyncStokeResponse(
        const transport::protobuf::Header& header,
        const protobuf::ElectMessage& ec_msg);

private:


    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> sync_nodes_map_;
    std::mutex sync_nodes_map_mutex_;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(NodesStokeManager);
};

}  // namespace elect

}  // namespace zjchain
