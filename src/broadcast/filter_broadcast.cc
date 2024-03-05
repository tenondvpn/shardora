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
    
}

std::shared_ptr<common::BloomFilter> FilterBroadcast::GetBloomfilter(
        const transport::protobuf::Header& message) {
   return nullptr;
}

std::vector<dht::NodePtr> FilterBroadcast::GetlayerNodes(
        dht::BaseDhtPtr& dht_ptr,
        std::shared_ptr<common::BloomFilter>& bloomfilter,
        const transport::protobuf::Header& message) {
    return {};
}

std::vector<dht::NodePtr> FilterBroadcast::GetRandomFilterNodes(
        dht::BaseDhtPtr& dht_ptr,
        std::shared_ptr<common::BloomFilter>& bloomfilter,
        const transport::protobuf::Header& message) {
    return {};
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
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        int res = transport::TcpTransport::Instance()->Send(
            thread_idx,
            nodes[i]->public_ip,
            nodes[i]->public_port,
            msg_ptr->header);
        BROAD_DEBUG("broadcast random send to: %s:%d, txhash: %lu, res: %u",
            nodes[i]->public_ip.c_str(),
            nodes[i]->public_port,
            msg_ptr->header.hash64(),
            res);
    }
}

void FilterBroadcast::LayerSend(
        uint8_t thread_idx,
        dht::BaseDhtPtr& dht_ptr,
        const transport::MessagePtr& msg_ptr,
        std::vector<dht::NodePtr>& nodes) {
}

uint64_t FilterBroadcast::GetLayerLeft(
        uint64_t layer_left,
        const transport::protobuf::Header& message) {
    return 0;
}

uint64_t FilterBroadcast::GetLayerRight(
        uint64_t layer_right,
        const transport::protobuf::Header& message) {
    return 0;
}

}  // namespace broadcast

}  // namespace zjchain
