#pragma once

#include <functional>

#include "block/account_manager.h"
#include "common/utils.h"
#include "common/string_utils.h"
#include "dht/dht_function.h"
#include "dht/dht_key.h"
#include "network/universal.h"
#include "network/network_utils.h"
#include "network/universal_manager.h"
#include "network/dht_manager.h"
#include "network/bootstrap.h"
#include "security/security.h"

namespace shardora {

namespace network {

typedef std::function<bool(
    uint32_t network_id,
    const std::string& node_id)> NetworkMemberCallback;

template<class DhtType>
class ShardNetwork {
public:
    ShardNetwork(
        uint32_t network_id,
        NetworkMemberCallback member_callback,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<protos::PrefixDb>& prefix_db);
    ~ShardNetwork();
    int Init(std::shared_ptr<block::AccountManager>& acc_mgr);
    void Destroy();
    dht::BaseDhtPtr GetDht() {
        return elect_dht_;
    }

private:
    int JoinUniversal();
    int JoinShard();
    bool IsThisNetworkNode(uint32_t network_id, const std::string& id);
    int JoinNewNodeValid(dht::NodePtr& node);
    int SignDhtMessage(
        const std::string& peer_pubkey,
        const std::string& append_data,
        std::string* enc_data,
        std::string* sign_ch,
        std::string* sign_re);

    dht::BaseDhtPtr universal_role_{ nullptr };
    dht::BaseDhtPtr elect_dht_{ nullptr };
    uint32_t sharding_id_{ network::kConsensusShardEndNetworkId };
    NetworkMemberCallback member_callback_{ nullptr };
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ShardNetwork);
};

template<class DhtType>
ShardNetwork<DhtType>::ShardNetwork(
        uint32_t network_id,
        NetworkMemberCallback member_callback,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<protos::PrefixDb>& prefix_db)
        : sharding_id_(network_id),
          member_callback_(member_callback),
          security_(security),
          prefix_db_(prefix_db) {
    common::GlobalInfo::Instance()->networks().push_back(sharding_id_);
}

template<class DhtType>
ShardNetwork<DhtType>::~ShardNetwork() {}

template<class DhtType>
int ShardNetwork<DhtType>::Init(
        std::shared_ptr<block::AccountManager>& acc_mgr) {
    acc_mgr_ = acc_mgr;
    if (JoinShard() != kNetworkSuccess) {
        return kNetworkJoinShardFailed;
    }

    return kNetworkSuccess;
}

template<class DhtType>
void ShardNetwork<DhtType>::Destroy() {
    if (universal_role_) {
        network::UniversalManager::Instance()->UnRegisterUniversal(sharding_id_);
//         universal_role_->Destroy();
//         universal_role_.reset();
    }

    if (elect_dht_) {
        network::DhtManager::Instance()->UnRegisterDht(sharding_id_);
//         elect_dht_->Destroy();
//         elect_dht_.reset();
    }
}

template<class DhtType>
bool ShardNetwork<DhtType>::IsThisNetworkNode(uint32_t network_id, const std::string& id) {
    if (network_id == common::GlobalInfo::Instance()->network_id() &&
            network_id >= network::kRootCongressNetworkId &&
            network_id < network::kConsensusShardEndNetworkId) {
        if (member_callback_ != nullptr) {
            if (member_callback_(network_id, id)) {
                return true;
            }
        }
    }

//     NETWORK_ERROR("IsThisNetworkNode check failed! network: %d, id: %s",
//         network_id, common::Encode::HexEncode(id).c_str());
    return false;
}


template<class DhtType>
int ShardNetwork<DhtType>::JoinNewNodeValid(dht::NodePtr& node) {
    if (!(sharding_id_ >= network::kRootCongressNetworkId &&
            sharding_id_ < network::kConsensusShardEndNetworkId)) {
        protos::AddressInfoPtr account_info = acc_mgr_->GetAccountInfo(node->id);
        if (account_info == nullptr) {
            ZJC_INFO("get address: %s failed!", common::Encode::HexEncode(node->id).c_str());
            return dht::kDhtError;
        }

        if (account_info->sharding_id() == sharding_id_ - network::kConsensusWaitingShardOffset) {
            if (member_callback_ != nullptr) {
                if (member_callback_(account_info->sharding_id(), node->id) ||
                        member_callback_(network::kRootCongressNetworkId, node->id)) {
                    return dht::kDhtError;
                }
            }

            ZJC_DEBUG("JoinNewNodeValid valid node sharding_id_: %u, id: %s",
                sharding_id_, common::Encode::HexEncode(node->id).c_str());
            return dht::kDhtSuccess;
        }

        return dht::kDhtError;
    }

    auto network_id = dht::DhtKeyManager::DhtKeyGetNetId(node->dht_key);
    if (!IsThisNetworkNode(network_id, node->id)) {
//         NETWORK_ERROR("node is not in this shard.");
        return dht::kDhtError;
    }

    ZJC_DEBUG("JoinNewNodeValid valid node sharding_id_: %u, id: %s",
        sharding_id_, common::Encode::HexEncode(node->id).c_str());
    return dht::kDhtSuccess;
}

template<class DhtType>
int ShardNetwork<DhtType>::JoinShard() {
    auto unversal_dht = network::UniversalManager::Instance()->GetUniversal(
        network::kUniversalNetworkId);
    if (unversal_dht == nullptr) {
        ZJC_DEBUG("get universal dht failed!");
        return kNetworkError;
    }

    assert(unversal_dht);
    assert(unversal_dht->local_node());
    auto local_node = std::make_shared<dht::Node>(
        sharding_id_,
        unversal_dht->local_node()->public_ip,
        unversal_dht->local_node()->public_port,
        unversal_dht->local_node()->pubkey_str,
        unversal_dht->local_node()->id);
    dht::DhtKeyManager dht_key(sharding_id_, security_->GetAddress());
    local_node->dht_key = dht_key.StrKey();
    local_node->dht_key_hash = common::Hash::Hash64(dht_key.StrKey());
    elect_dht_ = std::make_shared<DhtType>(local_node);
    if (elect_dht_->Init(
            security_,
            nullptr,
            std::bind(
                &ShardNetwork::JoinNewNodeValid,
                this,
                std::placeholders::_1)) != network::kNetworkSuccess) {
        NETWORK_ERROR("init shard role dht failed!");
        return kNetworkError;
    }

    network::DhtManager::Instance()->RegisterDht(sharding_id_, elect_dht_);
    auto boot_nodes = network::Bootstrap::Instance()->GetNetworkBootstrap(sharding_id_, 3);
    if (boot_nodes.empty()) {
        NETWORK_DEBUG("no bootstrap nodes.");
        return kNetworkSuccess;
    }

    if (elect_dht_->Bootstrap(boot_nodes, false, sharding_id_) != dht::kDhtSuccess) {
        NETWORK_ERROR("join shard network [%u] failed!", sharding_id_);
    }

    auto uni_net = UniversalManager::Instance()->GetUniversal(network::kUniversalNetworkId);
    std::vector<dht::NodePtr> nodes;
    auto dht_ptr = uni_net->readonly_hash_sort_dht();
    dht::DhtFunction::GetNetworkNodes(*dht_ptr, sharding_id_, nodes);
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        auto new_node = std::make_shared<dht::Node>(
            sharding_id_,
            nodes[i]->public_ip,
            nodes[i]->public_port,
            nodes[i]->pubkey_str,
            nodes[i]->id);
        elect_dht_->Join(new_node);
        ZJC_DEBUG("join network %u add new node: %s:%u, %s",
            sharding_id_,
            nodes[i]->public_ip.c_str(),
            nodes[i]->public_port,
            common::Encode::HexEncode(nodes[i]->id).c_str());
    }

    NETWORK_DEBUG("join network: %d success.", sharding_id_);
    return kNetworkSuccess;
}

}  // namespace network

}  // namespace shardora
