#pragma once

#include <memory>
#include <unordered_map>

#include "common/utils.h"
#include "common/log.h"
#include "protos/block.pb.h"

namespace shardora {

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
// [SYNC_OPT] Increased from 64 to 256: each sync request now carries 4x more
// keys, reducing round-trip overhead. A single view block is ~200-500 bytes,
// so 256 keys × 500B = 128KB — well within the 1MB packet limit.
static const uint32_t kEachRequestMaxSyncKeyCount = 256u;
// [SYNC_OPT] Increased from 3 to 7: contact more peers per sync round for
// better parallelism. With 33 pools falling behind, 3 peers was a severe
// bottleneck — each peer can only serve so many blocks per second.
static const uint32_t kSyncNeighborCount = 7u;
static const uint32_t kSyncTickPeriod = 1u * 1000u * 1000u;
static const uint32_t kSyncPacketMaxSize = 768u * 1024u;  // 768KB payload, leaves room for protobuf overhead within 1.5MB transport limit
// [SYNC_OPT] Increased from 1024 to 4096: process more pending sync items
// per PopItems() call. With 33 pools each needing hundreds of blocks,
// 1024 was exhausted in a single round.
static const uint32_t kSyncMaxKeyCount = 4096u;
static const uint32_t kSyncMaxRetryTimes = 7u;  // fail retry 7 times
static const uint32_t kPoolHeightPairCount = 2u * (common::kImmutablePoolSize + 1u);

}  // namespace sync

}  // namespace shardora
