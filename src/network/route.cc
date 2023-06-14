#include "network/route.h"

#include "common/time_utils.h"
#include "broadcast/filter_broadcast.h"
#include "dht/dht_key.h"
#include "network/universal.h"
#include "network/dht_manager.h"
#include "network/universal_manager.h"
#include "network/network_utils.h"
#include "transport/processor.h"

namespace zjchain {

namespace network {

Route* Route::Instance() {
    static Route ins;
    return &ins;
}

void Route::Init() {
//     auto thread_count = common::GlobalInfo::Instance()->message_handler_thread_count();
    broadcast_queue_ = new BroadcastQueue[common::kMaxThreadCount + 1];
    RegisterMessage(
            common::kDhtMessage,
            std::bind(&Route::HandleDhtMessage, this, std::placeholders::_1));
    RegisterMessage(
            common::kNetworkMessage,
            std::bind(&Route::HandleDhtMessage, this, std::placeholders::_1));
    broadcast_ = std::make_shared<broadcast::FilterBroadcast>();
    broadcast_thread_index_ = common::kMaxThreadCount;
    broadcast_thread_ = std::make_shared<std::thread>(std::bind(&Route::Broadcasting, this));
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
            assert(message.broadcast().bloomfilter_size() < 64);
//             broadcast_->Broadcasting(msg_ptr->thread_idx, dht_ptr, msg_ptr);
            broadcast_queue_[msg_ptr->thread_idx].push(msg_ptr);
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
    if (header.type() >= common::kLegoMaxMessageTypeCount) {
        return;
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

    auto dht = GetDht(header.des_dht_key());
    if (!dht) {
        RouteByUniversal(header_ptr);
        return;
    }

    if (header.has_broadcast()) {
//         Broadcast(header_ptr->thread_idx, header_ptr);
        ZJC_DEBUG("broadcast: %lu", header_ptr->header.hash64());
        broadcast_queue_[header_ptr->thread_idx].push(header_ptr);
        broadcast_con_.notify_one();
    }

    message_processor_[header.type()](header_ptr);
}

void Route::Broadcasting() {
    while (!destroy_) {
        bool has_data = false;
        for (uint32_t i = 0; i <= common::kMaxThreadCount; ++i) {
            while (broadcast_queue_[i].size() > 0) {
                transport::MessagePtr msg_ptr;
                if (broadcast_queue_[i].pop(&msg_ptr)) {
                    Broadcast(broadcast_thread_index_, msg_ptr);
                    if (!has_data) {
                        has_data = true;
                    }
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
    if (type >= common::kLegoMaxMessageTypeCount) {
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
    if (type >= common::kLegoMaxMessageTypeCount) {
        return;
    }

    message_processor_[type] = nullptr;
}

Route::Route() {
    Init();
}

Route::~Route() {
    Destroy();
}

void Route::Broadcast(uint8_t thread_idx, const transport::MessagePtr& msg_ptr) {
    auto& header = msg_ptr->header;
    if (!header.has_broadcast() || !header.has_des_dht_key()) {
        return;
    }

    uint32_t des_net_id = dht::DhtKeyManager::DhtKeyGetNetId(header.des_dht_key());
    auto des_dht = GetDht(header.des_dht_key());
    if (!des_dht) {
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
    broadcast_queue_[thread_idx].push(msg_ptr);
    broadcast_con_.notify_one();
//     broadcast_->Broadcasting(thread_idx, des_dht, msg_ptr);
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
        ZJC_DEBUG("now send to des net.");
        universal_dht->SendToDesNetworkNodes(msg_ptr);
    } else {
        universal_dht->SendToClosestNode(msg_ptr);
    }
}

}  // namespace network

}  // namespace zjchain
