#pragma once

#include <memory>
#include <vector>

#include "common/utils.h"
#include "common/config.h"
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
    void Init(std::shared_ptr<security::Security>& security, std::shared_ptr<db::Db>& db);
    void Destroy();
    void DropNode(const std::string& ip, uint16_t port);
    void Join(const dht::NodePtr& node);
    void OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block);

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
    std::shared_ptr<db::Db> db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(UniversalManager);
};

}  // namespace network

}  // namespace zjchain
