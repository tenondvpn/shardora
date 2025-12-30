#pragma once

#include "common/utils.h"
#include "common/log.h"
#include "common/encode.h"

#define TMBLOCK_DEBUG(fmt, ...) SHARDORA_DEBUG("[tmblock]" fmt, ## __VA_ARGS__)
#define TMBLOCK_INFO(fmt, ...) SHARDORA_INFO("[tmblock]" fmt, ## __VA_ARGS__)
#define TMBLOCK_WARN(fmt, ...) SHARDORA_WARN("[tmblock]" fmt, ## __VA_ARGS__)
#define TMBLOCK_ERROR(fmt, ...) SHARDORA_ERROR("[tmblock]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace timeblock {

enum RootErrorCode {
    kTimeBlockSuccess = 0,
    kTimeBlockError = 1,
    kTimeBlockVssError = 2,
};

static const uint64_t kTimeBlockTolerateSeconds = 30ll;
static const uint64_t kTimeBlockMaxOffsetSeconds = 10ll;
static const uint32_t kTimeBlockAvgCount = 6u;
static const uint64_t kCheckTimeBlockPeriodUs = 1000000llu;
static const uint64_t kCheckBftPeriodUs = 1000000llu;

}  // namespace timeblock

}  // namespace shardora
