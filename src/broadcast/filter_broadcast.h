#pragma once

#include "common/bloom_filter.h"
#include "dht/base_dht.h"
#include "broadcast/broadcast.h"

namespace zjchain {

namespace broadcast {

class FilterBroadcast : public Broadcast {
public:
    FilterBroadcast();
    virtual ~FilterBroadcast();
    virtual void Broadcasting(
            dht::BaseDhtPtr& dht_ptr,
            const transport::MessagePtr& message);

private:
    std::shared_ptr<common::BloomFilter> GetBloomfilter(
            const transport::protobuf::Header& message);
    std::vector<dht::NodePtr> GetlayerNodes(
            dht::BaseDhtPtr& dht_ptr,
            std::shared_ptr<common::BloomFilter>& bloomfilter,
            const transport::protobuf::Header& message);
    std::vector<dht::NodePtr> GetRandomFilterNodes(
            dht::BaseDhtPtr& dht_ptr,
            std::shared_ptr<common::BloomFilter>& bloomfilter,
            const transport::protobuf::Header& message);
    uint32_t BinarySearch(dht::Dht& dht, uint64_t val);
    void LayerSend(
            dht::BaseDhtPtr& dht_ptr,
            const transport::MessagePtr& message,
            std::vector<dht::NodePtr>& nodes);
    void Send(
            dht::BaseDhtPtr& dht_ptr,
            const transport::MessagePtr& message,
            const std::vector<dht::NodePtr>& nodes);
    uint64_t GetLayerLeft(uint64_t layer_left, const transport::protobuf::Header& message);
    uint64_t GetLayerRight(uint64_t layer_right, const transport::protobuf::Header& message);

    DISALLOW_COPY_AND_ASSIGN(FilterBroadcast);
};

}  // namespace broadcast

}  // namespace zjchain
