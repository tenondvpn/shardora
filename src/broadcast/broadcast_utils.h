#pragma once

#include "common/utils.h"
#include "common/log.h"

#define BROAD_DEBUG(fmt, ...) SHARDORA_DEBUG("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_INFO(fmt, ...) SHARDORA_INFO("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_WARN(fmt, ...) SHARDORA_WARN("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_ERROR(fmt, ...) SHARDORA_ERROR("[broadcast]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace broadcast {

static const uint32_t kBroadcastDefaultNeighborCount = 7u;
static const uint32_t kBloomfilterBitSize = 256u;
static const uint32_t kBloomfilterHashCount = 3u;
static const uint32_t kBroadcastHopLimit = 10u;
static const uint32_t kBroadcastHopToLayer = 1u;
static const uint32_t kBroadcastIgnBloomfilter = 1u;

static inline void SetDefaultBroadcastParam(transport::protobuf::BroadcastParam* broadcast) {
    broadcast->set_ign_bloomfilter_hop(kBroadcastIgnBloomfilter);
    broadcast->set_hop_to_layer(0);
    int32_t hop_limit = kBroadcastHopLimit;
    broadcast->set_hop_limit(hop_limit);
    broadcast->set_layer_left(0);
    broadcast->set_layer_right(common::kInvalidUint64);
    int32_t neigber_count = kBroadcastDefaultNeighborCount;
    broadcast->set_neighbor_count(neigber_count);
    float overlap = 0.5f;
    broadcast->set_overlap(overlap);
}

}  // namespace broadcast

}  // namespace shardora
