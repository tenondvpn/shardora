#include "network/dht_manager.h"

#include <cassert>
#include <algorithm>

#include "dht/base_dht.h"
#include "dht/dht_key.h"
#include "dht/dht_proto.h"
#include "network/network_utils.h"
#include "network/universal_manager.h"
#include "network/universal.h"

namespace zjchain {

namespace network {

DhtManager* DhtManager::Instance() {
    static DhtManager ins;
    return &ins;
}

void DhtManager::Init() {
    dhts_ = new dht::BaseDhtPtr[kConsensusWaitingShardEndNetworkId];
    std::fill(dhts_, dhts_ + kConsensusWaitingShardEndNetworkId, nullptr);
    dht_map_ptr_ = std::make_shared<std::unordered_map<uint32_t, dht::BaseDhtPtr>>(dht_map_);
}

void DhtManager::Destroy() {
    dht_map_.clear();
    dht_map_ptr_ = nullptr;
    if (dhts_ != nullptr) {
        for (uint32_t i = 0; i < kConsensusWaitingShardEndNetworkId; ++i) {
            if (dhts_[i] != nullptr) {
                dhts_[i]->Destroy();
                dhts_[i] = nullptr;
            }
        }

        delete []dhts_;
        dhts_ = nullptr;
    }
}

void DhtManager::RegisterDht(uint32_t net_id, dht::BaseDhtPtr& dht) {
    if (net_id >= kConsensusWaitingShardEndNetworkId) {
        ZJC_ERROR("invalid network id: %u", net_id);
        return;
    }

    if (dhts_[net_id] != nullptr) {
        ZJC_ERROR("dht has registered: %u", net_id);
        return;
    }

    dhts_[net_id] = dht;
    dht_map_[net_id] = dht;
    dht_map_ptr_ = std::make_shared<std::unordered_map<uint32_t, dht::BaseDhtPtr>>(dht_map_);
}

void DhtManager::UnRegisterDht(uint32_t net_id) {
    if (net_id >= kConsensusWaitingShardEndNetworkId) {
        return;
    }

    if (dhts_[net_id] == nullptr) {
        return;
    }

    assert(dhts_[net_id] != nullptr);
    dhts_[net_id]->Destroy();
    dhts_[net_id] = nullptr;
    auto iter = dht_map_.find(net_id);
    if (iter != dht_map_.end()) {
        dht_map_.erase(iter);
    }

    dht_map_ptr_ = std::make_shared<std::unordered_map<uint32_t, dht::BaseDhtPtr>>(dht_map_);
}

dht::BaseDhtPtr DhtManager::GetDht(uint32_t net_id) {
    if (net_id >= kConsensusWaitingShardEndNetworkId) {
        return nullptr;
    }

    return dhts_[net_id];
}

DhtManager::DhtManager() {
    Init();
}

DhtManager::~DhtManager() {
    Destroy();
}

void DhtManager::DropNode(const std::string& ip, uint16_t port) {
    auto dht_map_ptr = dht_map_ptr_;
    for (auto iter = dht_map_ptr->begin(); iter != dht_map_ptr->end(); ++iter) {
        iter->second->Drop(ip, port);
    }
}

void DhtManager::Join(const dht::NodePtr& node) {
    auto dht_map_ptr = dht_map_ptr_;
    for (auto iter = dht_map_ptr->begin(); iter != dht_map_ptr->end(); ++iter) {
        iter->second->UniversalJoin(node);
    }
}

}  // namespace network

}  // namespace zjchain
