#pragma once

#include <memory>
#include <vector>

#include "common/utils.h"
#include "common/config.h"
#include "common/node_members.h"
#include "dht/base_dht.h"
#include "network/universal.h"
#include "security/security.h"

namespace zjchain {

namespace network {

class UniversalManager {
public:
    static UniversalManager* Instance();
    void RegisterUniversal(uint32_t network_id, dht::BaseDhtPtr& dht);
    void UnRegisterUniversal(uint32_t network_id);
    dht::BaseDhtPtr GetUniversal(uint32_t network_id);
    int CreateUniversalNetwork(uint8_t thread_idx, const common::Config& config);
    int CreateNodeNetwork(uint8_t thread_idx, const common::Config& config);
    void Init(std::shared_ptr<security::Security>& security);
    void Destroy();
    int AddNodeToUniversal(dht::NodePtr& node);
    void DropNode(const std::string& ip, uint16_t port);
    void Join(const dht::NodePtr& node);
    void OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members);

private:
    UniversalManager();
    ~UniversalManager();
    int CreateNetwork(uint8_t thread_idx, uint32_t network_id, const common::Config& config);
    void DhtBootstrapResponseCallback(
        dht::BaseDht* dht_ptr,
        const dht::protobuf::DhtMessage& dht_msg);

    static const uint32_t kUniversalNetworkCount = 2;

    dht::BaseDhtPtr dhts_[kUniversalNetworkCount];  // just universal and node network
    std::shared_ptr<security::Security> security_ = nullptr;
    struct ElectItem {
        uint64_t height;
        std::unordered_set<std::string> id_set;
    };

    std::unordered_map<uint32_t, std::shared_ptr<ElectItem>> sharding_latest_height_map_;

    DISALLOW_COPY_AND_ASSIGN(UniversalManager);
};

}  // namespace network

}  // namespace zjchain
