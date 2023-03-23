#pragma once

#include "common/utils.h"
#include "common/log.h"

namespace zjchain {

namespace sync {

enum SyncErrorCode {
    kSyncSuccess = 0,
    kSyncError = 1,
    kSyncKeyExsits = 2,
    kSyncKeyAdded = 3,
    kSyncBlockReloaded = 4,
};

enum SyncPriorityType {
    kSyncPriLowest = 0,
    kSyncPriLow = 1,
    kSyncNormal = 2,
    kSyncHigh = 3,
    kSyncHighest = 4,
};

static const uint32_t kSyncValueRetryPeriod = 3 * 1000 * 1000u;  // Persist 3s
static const uint32_t kTimeoutCheckPeriod = 3000 * 1000u;  // Persist 3s
static const uint32_t kMaxSyncMapCapacity = 1000000u;
static const uint32_t kMaxSyncKeyCount = 64u;
static const uint32_t kSyncNeighborCount = 3u;
static const uint32_t kSyncTickPeriod = 1u * 1000u * 1000u;
static const uint32_t kSyncPacketMaxSize = 1u * 1024u * 1024u;  // sync data 1M
static const uint32_t kSyncMaxKeyCount = 1024u;
static const uint32_t kSyncMaxRetryTimes = 7u;  // fail retry 3 times
static const uint32_t kPoolHeightPairCount = 2u * (common::kImmutablePoolSize + 1u);
static const uint32_t kLeafMaxHeightCount = 1024u * 1024u;// 1024u * 1024u;  // each merkle block 1M
static const uint32_t kEachHeightTreeMaxByteSize = kLeafMaxHeightCount * 2u;  // each merkle block 1M
static const uint32_t kBranchMaxCount = kLeafMaxHeightCount / 64u;
static const uint32_t kHeightLevelItemMaxCount = 2 * kBranchMaxCount - 1;
static const uint64_t kLevelNodeValidHeights = 0xFFFFFFFFFFFFFFFFlu;

}  // namespace sync

}  // namespace zjchain
