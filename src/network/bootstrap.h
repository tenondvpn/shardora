#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/utils.h"
#include "network/universal_manager.h"
#include "network/universal.h"
#include "security/security.h"

namespace zjchain {

namespace dht {
    class Node;
    typedef std::shared_ptr<Node> NodePtr;
}  // namespace dht

namespace network {

class Bootstrap {
public:
    static Bootstrap* Instance();
    int Init(common::Config& config, std::shared_ptr<security::Security>& security);
    std::vector<dht::NodePtr> GetNetworkBootstrap(uint32_t network_id, uint32_t count);

    const std::vector<dht::NodePtr>& root_bootstrap() {
        return root_bootstrap_;
    }

    const std::vector<dht::NodePtr>& node_bootstrap() {
        return node_bootstrap_;
    }

private:
    Bootstrap() {}
    ~Bootstrap() {}
    std::vector<dht::NodePtr> root_bootstrap_;
    std::vector<dht::NodePtr> node_bootstrap_;

    DISALLOW_COPY_AND_ASSIGN(Bootstrap);
};

}  // namespace network

}  // namespace zjchain
