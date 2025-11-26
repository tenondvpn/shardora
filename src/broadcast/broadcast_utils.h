#pragma once

#include "common/utils.h"
#include "common/log.h"

#define BROAD_DEBUG(fmt, ...) SHARDORA_DEBUG("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_INFO(fmt, ...) SHARDORA_INFO("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_WARN(fmt, ...) SHARDORA_WARN("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_ERROR(fmt, ...) SHARDORA_ERROR("[broadcast]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace broadcast {

static const uint32_t kBroadcastDefaultNeighborCount = 13u;
static const uint32_t kBloomfilterBitSize = 256u;
static const uint32_t kBloomfilterHashCount = 3u;
static const uint32_t kBroadcastHopLimit = 10u;
static const uint32_t kBroadcastHopToLayer = 1u;
static const uint32_t kBroadcastIgnBloomfilter = 1u;

}  // namespace broadcast

}  // namespace shardora
