#pragma once

#include "common/utils.h"
#include "common/log.h"
#include "common/limit_heap.h"
#include "common/node_members.h"
#include "elect/elect_utils.h"

namespace zjchain {

namespace vss {

enum VssErrorCode {
    kVssSuccess = 0,
    kVssError = 1,
};

enum VssMessageType {
    kVssRandomHash = 1,
    kVssRandom = 2,
    kVssFinalRandom = 3,
};

static const int32_t kVssRandomSplitCount = 3u;
static const uint32_t kVssRandomduplicationCount = 7u;
static const int64_t kVssCheckPeriodTimeout = 3000000ll;
// Avoid small differences in time between different machines leading to cheating
static const uint32_t kVssTimePeriodOffsetSeconds = 3u;
static const uint32_t kHandleMessageVssTimePeriodOffsetSeconds = 1u;

struct ElectItem {
    ElectItem()
        : members(nullptr),
        local_index(elect::kInvalidMemberIndex),
        member_count(0),
        elect_height(0) {}
    common::MembersPtr members;
    uint32_t local_index;
    uint32_t member_count;
    uint64_t elect_height;
};

}  // namespace vss

}  // namespace zjchain
