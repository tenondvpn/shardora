#include "broadcast/filter_broadcast.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

#include "common/global_info.h"
#include "dht/base_dht.h"
#include "broadcast/broadcast_utils.h"

namespace shardora {

namespace broadcast {

FilterBroadcast::FilterBroadcast() {}

FilterBroadcast::~FilterBroadcast() {}

void FilterBroadcast::Broadcasting(
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& msg_ptr) {
    assert(dht_ptr);
    auto readonly_dht_ptr = dht_ptr->readonly_hash_sort_dht();
    if (readonly_dht_ptr->size() < 2u) {
        SHARDORA_DEBUG("random Broadcasting: %lu, size: %u, dht net: %d",
            msg_ptr->header.hash64(), readonly_dht_ptr->size(), dht_ptr->local_node()->sharding_id);
        // assert(false);
        return;
    }

    auto& message = msg_ptr->header;
    uint32_t now_hop_count = message.hop_count();
    // if (message.broadcast().has_hop_to_layer() &&
    //         now_hop_count >= message.broadcast().hop_to_layer()) {
    //     if (message.broadcast().layer_left() <= 0) {
    //         message.mutable_broadcast()->set_layer_left(0);
    //     }

    //     if (message.broadcast().layer_right() <= 0) {
    //         message.mutable_broadcast()->set_layer_right(common::kInvalidUint64);
    //     }
    // }

    if (message.broadcast().has_hop_limit() && message.broadcast().hop_limit() <= now_hop_count) {
        SHARDORA_DEBUG("message.broadcast().hop_limit() <= now_hop_count[%d, %d] hash: %lu",
            message.broadcast().hop_limit(), now_hop_count, message.hash64());
        return;
    }

    if (now_hop_count >= kBroadcastHopLimit) {
        return;
    }

    auto bloomfilter = GetBloomfilter(message);
    // if (message.broadcast().has_hop_to_layer() &&
    //         now_hop_count >= message.broadcast().hop_to_layer()) {
        auto nodes = GetlayerNodes(dht_ptr, bloomfilter, message);
        for (auto iter = nodes.begin(); iter != nodes.end(); ++iter) {
            bloomfilter->insert((*iter)->id_hash);
        }

        SHARDORA_DEBUG("layer Broadcasting: %lu, size: %u", msg_ptr->header.hash64(), nodes.size());
        // msg_ptr->header.mutable_broadcast()->clear_bloomfilter();
        // TODO(xielei): test gossip ,remove it later
        message.set_hop_count(now_hop_count + 1);
        LayerSend(dht_ptr, msg_ptr, nodes);
    // } else {
    //     auto nodes = GetRandomFilterNodes(dht_ptr, bloomfilter, message);
    //     // for (auto iter = nodes.begin(); iter != nodes.end(); ++iter) {
    //     //     bloomfilter->insert((*iter)->id_hash);
    //     // }

    //     SHARDORA_DEBUG("random Broadcasting: %lu, size: %u",
    //         msg_ptr->header.hash64(), nodes.size());
    //     if (msg_ptr->header.broadcast().bloomfilter_size() >= 64) {
    //         return;
    //     }

    //     // TODO(xielei): test gossip ,remove it later
    //     // msg_ptr->header.mutable_broadcast()->clear_bloomfilter();
    //     message.set_hop_count(now_hop_count + 1);
    //     Send(dht_ptr, msg_ptr, nodes);
    // }
}

std::shared_ptr<std::unordered_set<uint64_t>> FilterBroadcast::GetBloomfilter(
        const transport::protobuf::Header& message) {
    auto data_set = std::make_shared<std::unordered_set<uint64_t>>();
    if (message.broadcast().bloomfilter_size() <= 0) {
        return data_set;
    }

    for (auto i = 0; i < message.broadcast().bloomfilter_size(); ++i) {
        data_set->insert(message.broadcast().bloomfilter(i));
    }

    return data_set;
}

std::vector<dht::NodePtr> FilterBroadcast::GetlayerNodes(
        dht::BaseDhtPtr& dht_ptr,
        std::shared_ptr<std::unordered_set<uint64_t>>& bloomfilter,
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
    if (left >= right || right >= hash_order_dht->size()) {
        return {};
    }

    std::vector<uint32_t> pos_vec;
    uint32_t idx = 0;
    for (uint32_t i = left; i <= right; ++i) {
        pos_vec.push_back(i);
    }

    std::mt19937 rng(std::random_device{}());
    std::shuffle(pos_vec.begin(), pos_vec.end(), rng);
    std::vector<dht::NodePtr> nodes;
    uint32_t neighbor_count = GetNeighborCount(message);
    uint32_t now_hop_count = message.hop_count();
    bloomfilter->insert(dht_ptr->local_node()->id_hash);
    for (uint32_t i = 0; i < pos_vec.size(); ++i) {
        if (bloomfilter->find((*hash_order_dht)[pos_vec[i]]->id_hash) != bloomfilter->end()) {
            SHARDORA_DEBUG("bloom filtered: %s:%d, %lu, hash64: %lu",
                (*hash_order_dht)[pos_vec[i]]->public_ip.c_str(),
                (*hash_order_dht)[pos_vec[i]]->public_port,
                (*hash_order_dht)[pos_vec[i]]->id_hash,
                message.hash64());
            continue;
        }

        nodes.push_back((*hash_order_dht)[pos_vec[i]]);
        if (message.broadcast().ign_bloomfilter_hop() <= now_hop_count) {
            bloomfilter->insert((*hash_order_dht)[pos_vec[i]]->id_hash);
        }

        if (nodes.size() >= neighbor_count) {
            break;
        }
    }

    broad_param->clear_bloomfilter();
    for (auto iter = bloomfilter->begin(); iter != bloomfilter->end(); ++iter) {
        if (broad_param->bloomfilter_size() < 64) {
            broad_param->add_bloomfilter(*iter);
        }
    }

    std::sort(
            nodes.begin(),
            nodes.end(),
            [](const dht::NodePtr& lhs, const dht::NodePtr& rhs)->bool {
        return lhs->id_hash < rhs->id_hash;
    });

    SHARDORA_DEBUG("layer send pre_left: %lu, prev_right: %lu, left: %lu, right: %lu, nodes count: %u",
        broad_param->layer_left(), broad_param->layer_right(), 
        (*hash_order_dht)[left]->id_hash, (*hash_order_dht)[right]->id_hash, nodes.size());
    return nodes;
}

std::vector<dht::NodePtr> FilterBroadcast::GetRandomFilterNodes(
        dht::BaseDhtPtr& dht_ptr,
        std::shared_ptr<std::unordered_set<uint64_t>>& bloomfilter,
        const transport::protobuf::Header& message) {
    auto readobly_dht = dht_ptr->readonly_hash_sort_dht();
    std::vector<uint32_t> pos_vec;
    uint32_t idx = 0;
    for (uint32_t i = 0; i < readobly_dht->size(); ++i) {
        pos_vec.push_back(i);
    }

    std::mt19937 rng(std::random_device{}());
    std::shuffle(pos_vec.begin(), pos_vec.end(), rng);
    std::vector<dht::NodePtr> nodes;
    uint32_t neighbor_count = GetNeighborCount(message);
    uint32_t now_hop_count = message.hop_count();
    for (uint32_t i = 0; i < pos_vec.size(); ++i) {
        if (bloomfilter->find((*readobly_dht)[pos_vec[i]]->id_hash) != bloomfilter->end()) {
            SHARDORA_DEBUG("bloom filtered: %s:%d, %lu, hash64: %lu",
                (*readobly_dht)[pos_vec[i]]->public_ip.c_str(),
                (*readobly_dht)[pos_vec[i]]->public_port,
                (*readobly_dht)[pos_vec[i]]->id_hash,
                message.hash64());
            continue;
        }

        nodes.push_back((*readobly_dht)[pos_vec[i]]);
        SHARDORA_DEBUG("bloom filter add node: %s:%d, %lu, hash64: %lu, ign hop: %d, now hop: %d",
                (*readobly_dht)[pos_vec[i]]->public_ip.c_str(),
                (*readobly_dht)[pos_vec[i]]->public_port,
                (*readobly_dht)[pos_vec[i]]->id_hash,
                message.hash64(),
                message.broadcast().ign_bloomfilter_hop(),
                now_hop_count);
        if (message.broadcast().ign_bloomfilter_hop() <= now_hop_count) {
            bloomfilter->insert((*readobly_dht)[pos_vec[i]]->id_hash);
        }

        if (nodes.size() >= neighbor_count) {
            break;
        }
    }

    // SHARDORA_DEBUG("data size: %u, pos_vec size: %u, readobly_dht->size: %u",
    //     bloomfilter->data().size(), pos_vec.size(), readobly_dht->size());
    // for (uint32_t i = 0; i < bloomfilter->data().size(); ++i) {
    //     SHARDORA_DEBUG("data i: %d, data: %lu", i, bloomfilter->data()[i]);
    // }

    bloomfilter->insert(dht_ptr->local_node()->id_hash);
    auto cast_msg = const_cast<transport::protobuf::Header*>(&message);
    auto broad_param = cast_msg->mutable_broadcast();
    broad_param->clear_bloomfilter();
    for (auto iter = bloomfilter->begin(); iter != bloomfilter->end(); ++iter) {
        if (broad_param->bloomfilter_size() < 64) {
            broad_param->add_bloomfilter(*iter);
        }
    }

    return nodes;
}

uint32_t FilterBroadcast::BinarySearch(const dht::Dht& dht, uint64_t val) {
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
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& msg_ptr,
        const std::vector<dht::NodePtr>& nodes) {
    auto readobly_dht = dht_ptr->readonly_hash_sort_dht();
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        int res = transport::TcpTransport::Instance()->Send(
            nodes[i]->public_ip,
            nodes[i]->public_port,
            msg_ptr->header);
        SHARDORA_DEBUG("broadcast random send to: %s:%d, txhash: %lu, res: %u",
            nodes[i]->public_ip.c_str(),
            nodes[i]->public_port,
            msg_ptr->header.hash64(),
            res);
    }
}

void FilterBroadcast::LayerSend(
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

        SHARDORA_DEBUG("broadcast layer send to: %s:%d, txhash: %lu, src:  %lu, %.lu, new: %lu, %lu",
            nodes[i]->public_ip.c_str(), nodes[i]->public_port, msg_ptr->header.hash64(),
            src_left,
            src_right,
            broad_param->layer_left(),
            broad_param->layer_right());
        transport::TcpTransport::Instance()->Send(
            nodes[i]->public_ip,
            nodes[i]->public_port,
            message);
    }
}

uint64_t FilterBroadcast::GetLayerLeft(
        uint64_t layer_left,
        const transport::protobuf::Header& message) {
    if (layer_left == 0llu) {
        return 0llu;
    }

    float new_overlap = pow(message.broadcast().overlap(), message.hop_count());
    uint64_t tmp_left = layer_left;
    if (message.broadcast().has_overlap() &&
            fabs(new_overlap) > std::numeric_limits<float>::epsilon()) {
        layer_left -= static_cast<uint64_t>(
                (double)layer_left * (double)new_overlap);
    }
    return layer_left < tmp_left ? layer_left : tmp_left;
}

uint64_t FilterBroadcast::GetLayerRight(
        uint64_t layer_right,
        const transport::protobuf::Header& message) {
    if (layer_right == common::kInvalidUint64) {
        return common::kInvalidUint64;
    }

    float new_overlap = pow(message.broadcast().overlap(), message.hop_count());
    uint64_t tmp_right = layer_right;
    if (message.broadcast().has_overlap() &&
            fabs(new_overlap) > std::numeric_limits<float>::epsilon()) {
        layer_right += static_cast<uint64_t>(
                (double)layer_right * (double)new_overlap);
    }
    return layer_right > tmp_right ? layer_right : tmp_right;
}

}  // namespace broadcast

}  // namespace shardora
