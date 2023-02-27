#pragma once

#include "common/utils.h"
#include "common/log.h"
#include "common/encode.h"

#define TMBLOCK_DEBUG(fmt, ...) ZJC_DEBUG("[tmblock]" fmt, ## __VA_ARGS__)
#define TMBLOCK_INFO(fmt, ...) ZJC_INFO("[tmblock]" fmt, ## __VA_ARGS__)
#define TMBLOCK_WARN(fmt, ...) ZJC_WARN("[tmblock]" fmt, ## __VA_ARGS__)
#define TMBLOCK_ERROR(fmt, ...) ZJC_ERROR("[tmblock]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace tmblock {

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
static const std::string kAttrTimerBlock = "__tmblock_tmblock";
static const std::string kAttrTimerBlockHeight = "__tmblock_tmblock_height";
static const std::string kAttrTimerBlockTm = "__tmblock_tmblock_tm";
static const std::string kVssRandomAttr = "__vssrandomattr";

}  // namespace tmblock

}  // namespace zjchain
