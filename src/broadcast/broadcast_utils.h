#pragma once

#include "common/utils.h"
#include "common/log.h"

#define BROAD_DEBUG(fmt, ...) ZJC_DEBUG("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_INFO(fmt, ...) ZJC_INFO("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_WARN(fmt, ...) ZJC_WARN("[broadcast]" fmt, ## __VA_ARGS__)
#define BROAD_ERROR(fmt, ...) ZJC_ERROR("[broadcast]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace broadcast {

static const uint32_t kBroadcastDefaultNeighborCount = 7u;
static const uint32_t kBloomfilterBitSize = 256u;
static const uint32_t kBloomfilterHashCount = 3u;
static const uint32_t kBroadcastHopLimit = 10u;
static const uint32_t kBroadcastHopToLayer = 1u;
static const uint32_t kBroadcastIgnBloomfilter = 1u;

}  // namespace broadcast

}  // namespace zjchain
