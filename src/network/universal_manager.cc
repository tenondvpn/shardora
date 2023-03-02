#include "network/universal_manager.h"

#include <cassert>

#include "common/global_info.h"
#include "common/encode.h"
#include "common/country_code.h"
#include "common/time_utils.h"
//#include "ip/ip_with_country.h"
//#include "security/ecdsa/security.h"
#include "dht/dht_key.h"
//#include "init/update_vpn_init.h"
#include "network/network_utils.h"
#include "network/bootstrap.h"

namespace zjchain {

namespace network {

UniversalManager* UniversalManager::Instance() {
    static UniversalManager ins;
    return &ins;
}

void UniversalManager::Init(std::shared_ptr<security::Security>& security) {
    security_ = security;
}

void UniversalManager::Destroy() {
    for (uint32_t i = 0; i < kUniversalNetworkCount; ++i) {
        if (dhts_[i] != nullptr) {
            dhts_[i]->Destroy();
        }
    }
}

void UniversalManager::RegisterUniversal(uint32_t network_id, dht::BaseDhtPtr& dht) {
    if (network_id >= kUniversalNetworkCount) {
        ZJC_ERROR("invalid network id: %u", network_id);
        return;
    }

    if (dhts_[network_id] != nullptr) {
        // ZJC_ERROR("regiestered network id: %u", network_id);
        return;
    }

    dhts_[network_id] = dht;
    // ZJC_DEBUG("add universal network: %d", network_id);
}

void UniversalManager::UnRegisterUniversal(uint32_t network_id) {
    if (network_id >= kUniversalNetworkCount) {
        ZJC_ERROR("invalid network id: %u", network_id);
        return;
    }

    if (dhts_[network_id] != nullptr) {
        dhts_[network_id]->Destroy();
        dhts_[network_id] = nullptr;
    }
}

dht::BaseDhtPtr UniversalManager::GetUniversal(uint32_t network_id) {
    if (network_id >= kUniversalNetworkCount) {
//         ZJC_ERROR("invalid network id: %u", network_id);
        return nullptr;
    }

    return dhts_[network_id];
}

void UniversalManager::DhtBootstrapResponseCallback(
        dht::BaseDht* dht_ptr,
        const dht::protobuf::DhtMessage& dht_msg) {
    //if (dht_msg.bootstrap_res().has_init_message()) {
    //    init::UpdateVpnInit::Instance()->BootstrapInit(dht_msg.bootstrap_res().init_message());
    //}

    auto local_node = dht_ptr->local_node();
    NETWORK_ERROR("get local public ip: %s, publc_port: %d, res public port: %d",
        local_node->public_ip.c_str(),
        local_node->public_port,
        dht_msg.bootstrap_res().public_port());
    auto net_id = dht::DhtKeyManager::DhtKeyGetNetId(local_node->dht_key);
    auto local_dht_key = dht::DhtKeyManager(local_node->dht_key);
    if (net_id == network::kUniversalNetworkId) {
        /*auto node_country = ip::IpWithCountry::Instance()->GetCountryUintCode(
            local_node->public_ip());
        if (node_country != ip::kInvalidCountryCode) {
            local_dht_key.SetCountryId(node_country);
        } else {
            auto server_country_code = dht_msg.bootstrap_res().country_code();
            if (server_country_code != ip::kInvalidCountryCode) {
                node_country = server_country_code;
                local_dht_key.SetCountryId(server_country_code);
            }
        }

        common::GlobalInfo::Instance()->set_country(node_country);*/
    }
}

int UniversalManager::CreateNetwork(
        uint8_t thread_idx,
        uint32_t network_id,
        const common::Config& config) {
    dht::NodePtr local_node = std::make_shared<dht::Node>(
        network_id,
        common::GlobalInfo::Instance()->config_local_ip(),
        common::GlobalInfo::Instance()->config_local_port(),
        security_->GetPublicKey(),
        security_->GetAddress());
    local_node->first_node = common::GlobalInfo::Instance()->config_first_node();
    dht::BaseDhtPtr dht_ptr = std::make_shared<network::Universal>(local_node);
    dht_ptr->Init(
        security_,
        std::bind(
            &UniversalManager::DhtBootstrapResponseCallback,
            this,
            std::placeholders::_1,
            std::placeholders::_2),
        nullptr);
    //dht_ptr->SetBootstrapResponseCreateCallback(std::bind(
    //    &init::UpdateVpnInit::GetInitMessage,
    //    init::UpdateVpnInit::Instance(),
    //    std::placeholders::_1,
    //    std::placeholders::_2,
    //    std::placeholders::_3,
    //    std::placeholders::_4));
    RegisterUniversal(network_id, dht_ptr);
    if (local_node->first_node) {
        return kNetworkSuccess;
    }

    std::vector<dht::NodePtr> boot_nodes;
    if (network_id == kUniversalNetworkId) {
        boot_nodes = Bootstrap::Instance()->root_bootstrap();
    } else {
        boot_nodes = Bootstrap::Instance()->node_bootstrap();
    }

    uint64_t bTime = common::TimeUtils::TimestampMs();
    if (dht_ptr->Bootstrap(thread_idx, boot_nodes, false, network_id) != dht::kDhtSuccess) {
//         UnRegisterUniversal(network_id);
        NETWORK_ERROR("bootstrap universal network failed!");
        return kNetworkError;
    }

    NETWORK_ERROR("dht_ptr->Bootstrap use time: %lu!", (common::TimeUtils::TimestampMs() - bTime));
    return kNetworkSuccess;
}

int UniversalManager::CreateUniversalNetwork(uint8_t thread_idx, const common::Config& config) {
    int res = CreateNetwork(thread_idx, kUniversalNetworkId, config);
    if (res != kNetworkSuccess) {
        return res;
    }

    auto universal_dht = GetUniversal(kUniversalNetworkId);
    if (universal_dht == nullptr) {
        return kNetworkError;
    }

    return kNetworkSuccess;
}

int UniversalManager::CreateNodeNetwork(uint8_t thread_idx, const common::Config& config) {
    return CreateNetwork(thread_idx, kNodeNetworkId, config);
}

int UniversalManager::AddNodeToUniversal(dht::NodePtr& node) {
    auto universal_dht = GetUniversal(kUniversalNetworkId);
    if (universal_dht == nullptr) {
        return dht::kDhtSuccess;
    }

    node->join_way = dht::kJoinFromUnknown;
    int res = universal_dht->Join(node);
    return dht::kDhtSuccess;
}

UniversalManager::UniversalManager() {}

UniversalManager::~UniversalManager() {
    Destroy();
}

void UniversalManager::DropNode(const std::string& ip, uint16_t port) {
    for (uint32_t i = 0; i < kUniversalNetworkCount; ++i) {
        if (dhts_[i] != nullptr) {
            dhts_[i]->Drop(ip, port);
        }
    }
}

void UniversalManager::Join(const dht::NodePtr& node) {
    if (dhts_[kNodeNetworkId] != nullptr) {
        dhts_[kNodeNetworkId]->UniversalJoin(node);
    }
}

}  // namespace network

}  // namespace zjchain
