#include "network/universal.h"

#include "common/global_info.h"
#include "common/country_code.h"
#include "dht/dht_key.h"
#include "dht/dht_function.h"
#include "dht/dht_function.h"
#include "network/network_utils.h"
#include "network/universal_manager.h"
#include "network/dht_manager.h"

namespace zjchain {

namespace network {

Universal::Universal(dht::NodePtr& local_node, std::shared_ptr<db::Db>& db) : BaseDht(local_node) {
    if (local_node->sharding_id == network::kUniversalNetworkId) {
        is_universal_ = true;
    }

    prefix_db_ = std::make_shared<protos::PrefixDb>(db);
}

Universal::~Universal() {
    Destroy();
}

int Universal::Init(
        std::shared_ptr<security::Security>& security,
        dht::BootstrapResponseCallback boot_cb,
        dht::NewNodeJoinCallback node_join_cb) {
    security_ = security;
    if (BaseDht::Init(security, boot_cb, node_join_cb) != dht::kDhtSuccess) {
        NETWORK_ERROR("init base dht failed!");
        return kNetworkError;
    }

    universal_ids_ = new bool[kConsensusWaitingShardEndNetworkId];
    std::fill(universal_ids_, universal_ids_ + kConsensusWaitingShardEndNetworkId, false);

    uint32_t net_id = dht::DhtKeyManager::DhtKeyGetNetId(local_node_->dht_key);
    if (net_id == kUniversalNetworkId) {
        AddNetworkId(net_id);
    } else {
        dht::BaseDhtPtr dht = UniversalManager::Instance()->GetUniversal(kUniversalNetworkId);
        if (dht) {
            auto universal_dht = std::dynamic_pointer_cast<Universal>(dht);
            if (universal_dht) {
                universal_dht->AddNetworkId(net_id);
            }
        }
    }
    return kNetworkSuccess;
}

void Universal::HandleMessage(const transport::MessagePtr& msg_ptr) {
    auto& msg = msg_ptr->header;
    if (msg.type() == common::kDhtMessage) {
        return dht::BaseDht::HandleMessage(msg_ptr);
    }
}

int Universal::Join(dht::NodePtr& node) {
    int res = BaseDht::Join(node);
    if (!is_universal_) {
        return res;
    }

    AddNodeToUniversal(node);
    // add to subnetworks
//     ZJC_DEBUG("universal join node: %s:%d", node->public_ip.c_str(), node->public_port);
    DhtManager::Instance()->Join(node);
    UniversalManager::Instance()->Join(node);
    return res;
}

bool Universal::CheckDestination(const std::string& des_dht_key, bool closest) {
    if (dht::BaseDht::CheckDestination(des_dht_key, closest)) {
        return true;
    }

    uint32_t net_id = dht::DhtKeyManager::DhtKeyGetNetId(des_dht_key);
    if (!HasNetworkId(net_id)) {
        return false;
    }

    const auto& node = UniversalManager::Instance()->GetUniversal(net_id)->local_node();
    if (node->dht_key != des_dht_key) {
        return false;
    }
    return true;
}

std::vector<dht::NodePtr> Universal::LocalGetNetworkNodes(
        uint32_t network_id,
        uint32_t count) {
    std::vector<dht::NodePtr> tmp_nodes;
    auto dht = network::UniversalManager::Instance()->GetUniversal(network::kUniversalNetworkId);
    if (dht == nullptr) {
        return tmp_nodes;
    }

    auto tmp_dht_ptr = dht->readonly_hash_sort_dht();
    dht::DhtPtr local_nodes = std::make_shared<dht::Dht>(*tmp_dht_ptr);  // change must copy
    local_nodes->push_back(dht->local_node());
    for (uint32_t i = 0; i < local_nodes->size(); ++i) {
        auto net_id = dht::DhtKeyManager::DhtKeyGetNetId((*local_nodes)[i]->dht_key);
        if (net_id == network_id) {
            tmp_nodes.push_back((*local_nodes)[i]);
        }
    }

    return tmp_nodes;
}

void Universal::AddNetworkId(uint32_t network_id) {
    assert(network_id < kConsensusWaitingShardEndNetworkId);
    universal_ids_[network_id] = true;
}

void Universal::RemoveNetworkId(uint32_t network_id) {
    assert(network_id < kConsensusWaitingShardEndNetworkId);
    universal_ids_[network_id] = false;
}

bool Universal::HasNetworkId(uint32_t network_id) {
    assert(network_id < kConsensusWaitingShardEndNetworkId);
    return universal_ids_[network_id];
}

int Universal::Destroy() {
    if (universal_ids_ != nullptr) {
        delete []universal_ids_;
    }

    dht::BaseDhtPtr dht = UniversalManager::Instance()->GetUniversal(kUniversalNetworkId);
    if (dht == nullptr) {
        return kNetworkSuccess;
    }
    auto universal_dht = std::dynamic_pointer_cast<Universal>(dht);
    if (universal_dht == nullptr) {
        return kNetworkSuccess;
    }
    uint32_t net_id = dht::DhtKeyManager::DhtKeyGetNetId(local_node_->dht_key);
    universal_dht->RemoveNetworkId(net_id);
    return kNetworkSuccess;
}

void Universal::OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block) {
    auto iter = sharding_latest_height_map_.find(sharding_id);
    if (iter != sharding_latest_height_map_.end() && iter->second->height >= elect_height) {
        return;
    }

    auto new_item = std::make_shared<ElectItem>();
    new_item->height = elect_height;
    for (auto iter = members->begin(); iter != members->end(); ++iter) {
        new_item->id_set.insert((*iter)->id);
    }

    auto& in = elect_block->in();
    for (int32_t i = 0; i < in.size(); ++i) {
        auto id = security_->GetAddress(in[i].pubkey());
        new_item->id_set.insert(id);
    }

    sharding_latest_height_map_[sharding_id] = new_item;
    auto waiting_shard_id = sharding_id + network::kConsensusWaitingShardOffset;
    auto uni_net = UniversalManager::Instance()->GetUniversal(network::kUniversalNetworkId);
    auto dht_ptr = uni_net->readonly_hash_sort_dht();
    auto des_dht = DhtManager::Instance()->GetDht(sharding_id);
    std::set<std::string> old_id_set;
    for (auto iter = dht_ptr->begin(); iter < dht_ptr->end(); ++iter) {
        auto node = *iter;
        old_id_set.insert(node->id);
        if (new_item->id_set.find((*iter)->id) != new_item->id_set.end()) {
            if (des_dht != nullptr) {
                des_dht->UniversalJoin(*iter);
                ZJC_DEBUG("expand nodes join network %u add new node: %s:%u, %s",
                    sharding_id,
                    (*iter)->public_ip.c_str(),
                    (*iter)->public_port,
                    common::Encode::HexEncode((*iter)->id).c_str());
            }

            auto tmp_shard_id = dht::DhtKeyManager::DhtKeyGetNetId((*iter)->dht_key);
            if (tmp_shard_id != sharding_id &&
                    tmp_shard_id != network::kUniversalNetworkId &&
                    tmp_shard_id != network::kNodeNetworkId) {
                auto node = *iter;
                uni_net->Drop(node);
                ZJC_DEBUG("drop universal nodes network %u node: %s:%u, %s",
                    tmp_shard_id,
                    (*iter)->public_ip.c_str(),
                    (*iter)->public_port,
                    common::Encode::HexEncode((*iter)->id).c_str());
            }
        }
    }

    if (des_dht != nullptr) {
        for (auto iter = old_id_set.begin(); iter != old_id_set.end(); ++iter) {
            auto fiter = new_item->id_set.find(*iter);
            if (fiter == new_item->id_set.end()) {
                des_dht->Drop(*iter);
                ZJC_DEBUG("drop nodes network %u node: %s",
                    sharding_id,
                    common::Encode::HexEncode((*iter)).c_str());
            }
        }
    }

    auto wait_dht = DhtManager::Instance()->GetDht(waiting_shard_id);
    if (wait_dht != nullptr) {
        auto dht_ptr = uni_net->readonly_hash_sort_dht();
        for (auto iter = dht_ptr->begin(); iter < dht_ptr->end(); ++iter) {
            auto node = *iter;
            if (new_item->id_set.find((*iter)->id) != new_item->id_set.end()) {
                wait_dht->Drop((*iter)->id);
                ZJC_DEBUG("drop nodes network %u node: %s:%u, %s",
                    sharding_id,
                    (*iter)->public_ip.c_str(),
                    (*iter)->public_port,
                    common::Encode::HexEncode((*iter)->id).c_str());
            }
        }
    }
}

int Universal::AddNodeToUniversal(dht::NodePtr& node) {
    bool elected = false;
    for (auto sharding_iter = sharding_latest_height_map_.begin();
        sharding_iter != sharding_latest_height_map_.end(); ++sharding_iter) {
        auto id_iter = sharding_iter->second->id_set.find(node->id);
        if (id_iter != sharding_iter->second->id_set.end()) {
            auto new_node = std::make_shared<dht::Node>(
                sharding_iter->first,
                node->public_ip,
                node->public_port,
                node->pubkey_str,
                node->id);
            BaseDht::Join(new_node);
            elected = true;
            ZJC_DEBUG("universal add node: %s, sharding id: %u",
                common::Encode::HexEncode(node->id).c_str(), sharding_iter->first);
        }
    }

    if (!elected &&
            common::GlobalInfo::Instance()->network_id() >= network::kRootCongressNetworkId &&
            common::GlobalInfo::Instance()->network_id() < network::kConsensusShardEndNetworkId) {
        auto account_info = prefix_db_->GetAddressInfo(node->id);
        if (account_info != nullptr) {
            auto new_node = std::make_shared<dht::Node>(
                account_info->sharding_id() + network::kConsensusWaitingShardOffset,
                node->public_ip,
                node->public_port,
                node->pubkey_str,
                node->id);
            BaseDht::Join(new_node);
            auto root_new_node = std::make_shared<dht::Node>(
                network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset,
                node->public_ip,
                node->public_port,
                node->pubkey_str,
                node->id);
            BaseDht::Join(root_new_node);
        }
    }

    return dht::kDhtSuccess;
}

}  // namespace network

}  //namespace zjchain
