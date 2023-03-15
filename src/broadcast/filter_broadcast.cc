#include "broadcast/filter_broadcast.h"

#include <cmath>
#include <algorithm>
#include <functional>

#include "common/global_info.h"
#include "dht/base_dht.h"
#include "broadcast/broadcast_utils.h"

namespace zjchain {

namespace broadcast {

FilterBroadcast::FilterBroadcast() {}

FilterBroadcast::~FilterBroadcast() {}

void FilterBroadcast::Broadcasting(
        uint8_t thread_idx,
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& msg_ptr) {
    assert(dht_ptr);
    auto& message = msg_ptr->header;
    if (message.broadcast().hop_limit() <= message.hop_count()) {
//         BROAD_INFO("message.broadcast().hop_limit() <= message.hop_count()[%d, %d].",
//             message.broadcast().hop_limit(), message.hop_count());
        return;
    }

    auto bloomfilter = GetBloomfilter(message);
    bloomfilter->Add(dht_ptr->local_node()->id_hash);
    if (message.broadcast().has_hop_to_layer() &&
            message.hop_count() >= message.broadcast().hop_to_layer()) {
        auto nodes = GetlayerNodes(dht_ptr, bloomfilter, message);
        for (auto iter = nodes.begin(); iter != nodes.end(); ++iter) {
            bloomfilter->Add((*iter)->id_hash);
        }

        LayerSend(thread_idx, dht_ptr, msg_ptr, nodes);
    } else {
        auto nodes = GetRandomFilterNodes(dht_ptr, bloomfilter, message);
        for (auto iter = nodes.begin(); iter != nodes.end(); ++iter) {
            bloomfilter->Add((*iter)->id_hash);
        }

        Send(thread_idx, dht_ptr, msg_ptr, nodes);
    }
}

std::shared_ptr<common::BloomFilter> FilterBroadcast::GetBloomfilter(
        const transport::protobuf::Header& message) {
    if (message.broadcast().bloomfilter_size() <= 0) {
        return std::make_shared<common::BloomFilter>(
                kBloomfilterBitSize,
                kBloomfilterHashCount);
    }

    std::vector<uint64_t> data;
    for (auto i = 0; i < message.broadcast().bloomfilter_size(); ++i) {
        data.push_back(message.broadcast().bloomfilter(i));
    }
    return std::make_shared<common::BloomFilter>(data, kBloomfilterHashCount);
}

std::vector<dht::NodePtr> FilterBroadcast::GetlayerNodes(
        dht::BaseDhtPtr& dht_ptr,
        std::shared_ptr<common::BloomFilter>& bloomfilter,
        const transport::protobuf::Header& message) {
    auto cast_msg = const_cast<transport::protobuf::Header*>(&message);
    auto broad_param = cast_msg->mutable_broadcast();
    auto hash_order_dht = dht_ptr->readonly_hash_sort_dht();
    if (hash_order_dht == nullptr || hash_order_dht->empty()) {
        return std::vector<dht::NodePtr>();
    }

    auto layer_left = GetLayerLeft(broad_param->layer_left(), message);
    auto layer_right = GetLayerRight(broad_param->layer_right(), message);
    uint32_t left = BinarySearch(*hash_order_dht, layer_left);
    uint32_t right = BinarySearch(*hash_order_dht, layer_right);
    assert(right >= left);
    assert(right < hash_order_dht->size());
    std::vector<uint32_t> pos_vec;
    uint32_t idx = 0;
    for (uint32_t i = left; i <= right; ++i) {
        pos_vec.push_back(i);
    }

    std::random_shuffle(pos_vec.begin(), pos_vec.end());
    std::vector<dht::NodePtr> nodes;
    uint32_t neighbor_count = GetNeighborCount(message);
    for (uint32_t i = 0; i < pos_vec.size(); ++i) {
        if (bloomfilter->Contain((*hash_order_dht)[pos_vec[i]]->id_hash)) {
            continue;
        }

        nodes.push_back((*hash_order_dht)[pos_vec[i]]);
        if (message.broadcast().ign_bloomfilter_hop() <= message.hop_count() + 1) {
            bloomfilter->Add((*hash_order_dht)[pos_vec[i]]->id_hash);
        }

        if (nodes.size() >= neighbor_count) {
            break;
        }
    }

    auto& data = bloomfilter->data();
    for (uint32_t i = 0; i < data.size(); ++i) {
        broad_param->add_bloomfilter(data[i]);
    }

    std::sort(
            nodes.begin(),
            nodes.end(),
            [](const dht::NodePtr& lhs, const dht::NodePtr& rhs)->bool {
        return lhs->id_hash < rhs->id_hash;
    });
    return nodes;
}

std::vector<dht::NodePtr> FilterBroadcast::GetRandomFilterNodes(
        dht::BaseDhtPtr& dht_ptr,
        std::shared_ptr<common::BloomFilter>& bloomfilter,
        const transport::protobuf::Header& message) {
    dht::DhtPtr readobly_dht = dht_ptr->readonly_hash_sort_dht();
    std::vector<uint32_t> pos_vec;
    uint32_t idx = 0;
    for (uint32_t i = 0; i < readobly_dht->size(); ++i) {
        pos_vec.push_back(i);
    }
    std::random_shuffle(pos_vec.begin(), pos_vec.end());
    std::vector<dht::NodePtr> nodes;
    uint32_t neighbor_count = GetNeighborCount(message);
    for (uint32_t i = 0; i < pos_vec.size(); ++i) {
        if (bloomfilter->Contain((*readobly_dht)[pos_vec[i]]->id_hash)) {
            continue;
        }
        nodes.push_back((*readobly_dht)[pos_vec[i]]);
        if (message.broadcast().ign_bloomfilter_hop() <= message.hop_count() + 1) {
            bloomfilter->Add((*readobly_dht)[pos_vec[i]]->id_hash);
        }

        if (nodes.size() >= neighbor_count) {
            break;
        }
    }

    auto& data = bloomfilter->data();
    auto cast_msg = const_cast<transport::protobuf::Header*>(&message);
    auto broad_param = cast_msg->mutable_broadcast();
    for (uint32_t i = 0; i < data.size(); ++i) {
        broad_param->add_bloomfilter(data[i]);
    }
    return nodes;
}

uint32_t FilterBroadcast::BinarySearch(dht::Dht& dht, uint64_t val) {
    assert(!dht.empty());
    int32_t low = 0;
    int32_t high = dht.size() - 1;
    int mid = 0;
    while (low <= high) {
        mid = (low + high) / 2;
        if (dht[mid]->id_hash < val) {
            low = mid + 1;
        } else if (dht[mid]->id_hash > val) {
            high = mid - 1;
        } else {
            break;
        }
    }

    if (dht[mid]->id_hash > val && mid > 0) {
        --mid;
    }
    return mid;
}

void FilterBroadcast::Send(
        uint8_t thread_idx,
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& msg_ptr,
        const std::vector<dht::NodePtr>& nodes) {
    dht::DhtPtr readobly_dht = dht_ptr->readonly_hash_sort_dht();
    for (uint32_t i = 0; i < nodes.size(); ++i) {
//         BROAD_INFO("random send to: %s:%d", nodes[i]->public_ip.c_str(), nodes[i]->public_port);
        transport::TcpTransport::Instance()->Send(
            thread_idx,
            nodes[i]->public_ip,
            nodes[i]->public_port,
            msg_ptr->header);
    }
}

void FilterBroadcast::LayerSend(
        uint8_t thread_idx,
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& msg_ptr,
        std::vector<dht::NodePtr>& nodes) {
    auto& message = msg_ptr->header;
    auto cast_msg = const_cast<transport::protobuf::Header*>(&message);
    auto broad_param = cast_msg->mutable_broadcast();
    uint64_t src_left = broad_param->layer_left();
    uint64_t src_right = broad_param->layer_right();
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        if (i == 0) {
            broad_param->set_layer_left(GetLayerLeft(src_left, message));
            if (nodes.size() == 1) {
                broad_param->set_layer_right(GetLayerRight(src_right, message));
            } else {
                broad_param->set_layer_right(GetLayerRight(nodes[i]->id_hash, message));
            }
        }

        if (i > 0 && i < (nodes.size() - 1)) {
            broad_param->set_layer_left(GetLayerLeft(nodes[i - 1]->id_hash, message));
            broad_param->set_layer_right(GetLayerRight(nodes[i]->id_hash, message));
        }

        if (i > 0 && i == (nodes.size() - 1)) {
            broad_param->set_layer_left(GetLayerLeft(nodes[i - 1]->id_hash, message));
            broad_param->set_layer_right(GetLayerRight(src_right, message));
        }

//         BROAD_INFO("layer send to %s:%d", nodes[i]->public_ip.c_str(), nodes[i]->public_port);
        transport::TcpTransport::Instance()->Send(
            thread_idx,
            nodes[i]->public_ip,
            nodes[i]->public_port,
            message);
    }
}

uint64_t FilterBroadcast::GetLayerLeft(
        uint64_t layer_left,
        const transport::protobuf::Header& message) {
    uint64_t tmp_left = layer_left;
    if (message.broadcast().has_overlap() &&
            fabs(message.broadcast().overlap()) > std::numeric_limits<float>::epsilon()) {
        layer_left -= static_cast<uint64_t>(
                (double)layer_left * (double)message.broadcast().overlap());
    }
    return layer_left < tmp_left ? layer_left : tmp_left;
}

uint64_t FilterBroadcast::GetLayerRight(
        uint64_t layer_right,
        const transport::protobuf::Header& message) {
    uint64_t tmp_right = layer_right;
    if (message.broadcast().has_overlap() &&
            fabs(message.broadcast().overlap()) > std::numeric_limits<float>::epsilon()) {
        layer_right += static_cast<uint64_t>(
                (double)layer_right * (double)message.broadcast().overlap());
    }
    return layer_right > tmp_right ? layer_right : tmp_right;
}

}  // namespace broadcast

}  // namespace zjchain
