#include "network/route.h"

#include "common/time_utils.h"
#include "broadcast/filter_broadcast.h"
#include "dht/dht_key.h"
#include "network/universal.h"
#include "network/dht_manager.h"
#include "network/universal_manager.h"
#include "network/network_utils.h"
#include "transport/processor.h"

namespace shardora {

namespace network {

Route* Route::Instance() {
    static Route ins;
    return &ins;
}

void Route::Init(std::shared_ptr<security::Security> sec_ptr) {
    sec_ptr_ = sec_ptr;
    broadcast_queue_ = new BroadcastQueue[common::kMaxThreadCount];
    RegisterMessage(
            common::kDhtMessage,
            std::bind(&Route::HandleDhtMessage, this, std::placeholders::_1));
    RegisterMessage(
            common::kNetworkMessage,
            std::bind(&Route::HandleDhtMessage, this, std::placeholders::_1));
    broadcast_ = std::make_shared<broadcast::FilterBroadcast>();
    broadcast_thread_ = std::make_shared<std::thread>(std::bind(
        &Route::Broadcasting, 
        this));
}

void Route::Destroy() {
    destroy_ = true;
    if (broadcast_thread_ != nullptr) {
        broadcast_thread_->join();
        broadcast_thread_ = nullptr;
    }

    UnRegisterMessage(common::kDhtMessage);
    UnRegisterMessage(common::kNetworkMessage);
    broadcast_.reset();
    delete[] broadcast_queue_;
}

int Route::Send(const transport::MessagePtr& msg_ptr) {
    if (msg_ptr == nullptr) {
        return kNetworkError;
    }

    auto& message = msg_ptr->header;
    uint32_t des_net_id = dht::DhtKeyManager::DhtKeyGetNetId(message.des_dht_key());
    dht::BaseDhtPtr dht_ptr{ nullptr };
    if (des_net_id == network::kUniversalNetworkId || des_net_id == network::kNodeNetworkId) {
        dht_ptr = UniversalManager::Instance()->GetUniversal(des_net_id);
    } else {
        dht_ptr = DhtManager::Instance()->GetDht(des_net_id);
    }

    if (dht_ptr != nullptr) {
        if (message.has_broadcast()) {
            auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
            assert(message.broadcast().bloomfilter_size() < 64);
//             broadcast_->Broadcasting(msg_ptr->thread_idx, dht_ptr, msg_ptr);
            ZJC_DEBUG("0 broadcast: %lu, now size: %u", msg_ptr->header.hash64(), broadcast_queue_[thread_idx].size());
            broadcast_queue_[thread_idx].push(msg_ptr);
            broadcast_con_.notify_one();
        } else {
            dht_ptr->SendToClosestNode(msg_ptr);
        }
        return kNetworkSuccess;
    }

    // this node not in this network, relay by universal
    RouteByUniversal(msg_ptr);
    if (message.type() == common::kElectMessage) {
        NETWORK_ERROR("TTTTTTTT message.type() == common::kElectMessage des_net_id: %d send to universal.", des_net_id);
    }

    return kNetworkSuccess;
}

void Route::HandleMessage(const transport::MessagePtr& header_ptr) {
    auto& header = header_ptr->header;
    if (header.type() >= common::kMaxMessageTypeCount) {
        return;
    }

    if (header.has_broadcast() && !header_ptr->header_str.empty()) {
//         Broadcast(header_ptr->thread_idx, header_ptr);
        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        // ZJC_INFO("====5 broadcast t: %lu, hash: %lu, now size: %u", thread_idx, header_ptr->header.hash64(), broadcast_queue_[thread_idx].size());
        broadcast_queue_[thread_idx].push(header_ptr);
        broadcast_con_.notify_one();
    }

    if (message_processor_[header.type()] == nullptr) {
        RouteByUniversal(header_ptr);
        return;
    }

    auto uni_dht = network::UniversalManager::Instance()->GetUniversal(
            kUniversalNetworkId);
    if (!uni_dht) {
        return;
    }

    auto dht_ptr = GetDht(header.des_dht_key());
    if (!dht_ptr) {
        RouteByUniversal(header_ptr);
        return;
    }

    if (header.type() == common::kPoolsMessage) {
        if (!CheckPoolsMessage(header_ptr, dht_ptr)) {
            return;
        }
    }

    message_processor_[header.type()](header_ptr);
}

bool Route::CheckPoolsMessage(const transport::MessagePtr& header_ptr, dht::BaseDhtPtr dht_ptr) {
    auto& header = header_ptr->header;
    if (header.has_broadcast()) {
        assert(false);
        ZJC_DEBUG("pools message check route coming has broadcast.");
        return false;
    }

    if (header_ptr->header.has_sync_heights()) {
        return true;
    }

    if (header_ptr->address_info == nullptr || header_ptr->msg_hash.empty()) {
        ZJC_FATAL("pools message must verify signature and has address info.");
        return false;
    }

    // TODO: check is this node tx message or route to nearest consensus node
    if (header_ptr->address_info->sharding_id() != common::GlobalInfo::Instance()->network_id()) {
        RouteByUniversal(header_ptr);
        // ZJC_DEBUG("pools message check route coming network invalid.");
        return false;
    }

    auto members = all_shard_members_[common::GlobalInfo::Instance()->network_id()];
    if (members == nullptr) {
        dht_ptr->SendToClosestNode(header_ptr);
        // ZJC_DEBUG("pools message check route coming no members.");
        return false;
    }

    // auto store_member_index = common::Hash::Hash32(header_ptr->msg_hash) % members->size();
    // if ((*members)[store_member_index]->id != sec_ptr_->GetAddress()) {
    //     dht::DhtKeyManager dht_key(
    //         header_ptr->address_info->sharding_id(), 
    //         (*members)[store_member_index]->id);
    //     header.set_des_dht_key(dht_key.StrKey());
    //     dht_ptr->SendToClosestNode(header_ptr);
    //     // ZJC_DEBUG("pools message check route coming not this node.");
    //     return false;
    // }

    // ZJC_DEBUG("pools message check route coming success this node.");
    return true;
}

void Route::OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block) {
    if (elect_height <= latest_elect_height_[sharding_id]) {
        return;
    }

    latest_elect_height_[sharding_id] = elect_height;
    all_shard_members_[sharding_id] = members;
}

void Route::Broadcasting() {
    common::GlobalInfo::Instance()->get_thread_index();
    while (!destroy_) {
        bool has_data = false;
        for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
            while (true) {
                transport::MessagePtr msg_ptr = nullptr;
                if (!broadcast_queue_[i].pop(&msg_ptr) || msg_ptr == nullptr) {
                    break;
                }
            
                // msg_ptr->header.mutable_broadcast()->set_hop_to_layer(1);
                Broadcast(msg_ptr);
                if (!has_data) {
                    has_data = true;
                }
            }
        }

        if (!has_data) {
            std::unique_lock<std::mutex> lock(broadcast_mu_);
            broadcast_con_.wait_for(lock, std::chrono::milliseconds(10));
        }
    }
}

void Route::HandleDhtMessage(const transport::MessagePtr& header_ptr) {
    auto& header = header_ptr->header;
    auto dht = GetDht(header.des_dht_key());
    if (!dht) {
        NETWORK_ERROR("get dht failed!");
        return;
    }

    dht->HandleMessage(header_ptr);
}

void Route::RegisterMessage(uint32_t type, transport::MessageProcessor proc) {
    if (type >= common::kMaxMessageTypeCount) {
        return;
    }

    if (message_processor_[type] != nullptr) {
        NETWORK_ERROR("message handler exist.[%d]", type);
    }

    message_processor_[type] = proc;
    transport::Processor::Instance()->RegisterProcessor(
            type,
            std::bind(&Route::HandleMessage, this, std::placeholders::_1));
}

void Route::UnRegisterMessage(uint32_t type) {
    if (type >= common::kMaxMessageTypeCount) {
        return;
    }

    message_processor_[type] = nullptr;
}

Route::Route() {
}

Route::~Route() {
    Destroy();
}

void Route::Broadcast(const transport::MessagePtr& msg_ptr) {
    transport::protobuf::Header header;
    if (!header.ParseFromString(msg_ptr->header_str)) {
        return;
    }
    
    if (!header.has_broadcast() || !header.has_des_dht_key()) {
        ZJC_WARN("broadcast error: %lu", header.hash64());
        return;
    }

    uint32_t des_net_id = dht::DhtKeyManager::DhtKeyGetNetId(header.des_dht_key());
    auto des_dht = GetDht(header.des_dht_key());
    if (!des_dht) {
        RouteByUniversal(msg_ptr);
        ZJC_WARN("broadcast by universal error: %lu", header.hash64());
        return;
    }

    uint32_t src_net_id = kConsensusShardEndNetworkId;
    if (header.has_src_sharding_id()) {
        src_net_id = header.src_sharding_id();
    }

    if (src_net_id != des_net_id) {
        auto* cast_msg = const_cast<transport::protobuf::Header*>(&header);
        auto broad_param = cast_msg->mutable_broadcast();
        if (!broad_param->net_crossed()) {
            broad_param->set_net_crossed(true);
            broad_param->clear_bloomfilter();
            cast_msg->set_hop_count(0);
        }
    }

    assert(msg_ptr->header.broadcast().bloomfilter_size() < 64);
    ZJC_DEBUG("broadcast success: %lu", header.hash64());
    broadcast_->Broadcasting(des_dht, msg_ptr);
}

dht::BaseDhtPtr Route::GetDht(const std::string& dht_key) {
    uint32_t net_id = dht::DhtKeyManager::DhtKeyGetNetId(dht_key);
    dht::BaseDhtPtr dht = nullptr;
    if (net_id == kUniversalNetworkId || net_id == kNodeNetworkId) {
        dht = UniversalManager::Instance()->GetUniversal(net_id);
    } else {
        dht = DhtManager::Instance()->GetDht(net_id);
    }

    return dht;
}

void Route::RouteByUniversal(const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    auto universal_dht = UniversalManager::Instance()->GetUniversal(kUniversalNetworkId);
    if (!universal_dht) {
        return;
    }

    if (header.has_broadcast()) {
        // choose limit nodes to broadcast from universal
        universal_dht->SendToDesNetworkNodes(msg_ptr);
    } else {
        universal_dht->SendToClosestNode(msg_ptr);
    }
}

}  // namespace network

}  // namespace shardora
