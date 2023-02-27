#pragma once

#include <memory>
#include <unordered_map>

#include "common/utils.h"
#include "common/tick.h"
#include "dht/base_dht.h"

namespace zjchain {

namespace network {

class DhtManager {
public:
    static DhtManager* Instance();
    void RegisterDht(uint32_t net_id, dht::BaseDhtPtr& dht);
    void UnRegisterDht(uint32_t net_id);
    dht::BaseDhtPtr GetDht(uint32_t net_id);
    void Init();
    void Destroy();
    void DropNode(const std::string& ip, uint16_t port);
    void Join(const dht::NodePtr& node);

private:
    DhtManager();
    ~DhtManager();

    static const uint32_t kNetworkDetectPeriod = 3 * 1000 * 1000;
    static const uint32_t kNetworkDetectionLimitNum = 16;

    dht::BaseDhtPtr* dhts_{ nullptr };
    std::unordered_map<uint32_t, dht::BaseDhtPtr> dht_map_;
    std::shared_ptr<std::unordered_map<uint32_t, dht::BaseDhtPtr>> dht_map_ptr_;

    DISALLOW_COPY_AND_ASSIGN(DhtManager);
};

}  // namespace network

}  // namespace zjchain
