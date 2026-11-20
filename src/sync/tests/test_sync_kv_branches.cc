#include <gtest/gtest.h>
#include <string>
#include "common/utils.h"
#include "sync/sync_utils.h"

#define private public
#include "sync/key_value_sync.h"
#undef private

namespace shardora {
namespace sync {
namespace test {

TEST(SyncKvBranches, SyncItemTagEnumValues) {
    EXPECT_EQ(static_cast<uint32_t>(kBlockHeight), 1u);
    EXPECT_EQ(static_cast<uint32_t>(kViewHash), 2u);
    EXPECT_EQ(static_cast<uint32_t>(kBlockView), 3u);
}

TEST(SyncKvBranches, SyncItemHashKeyConstructor) {
    SyncItem item(10u, 2u, 100ull, 3u, kBlockHeight);
    EXPECT_EQ(item.network_id, 10u);
    EXPECT_EQ(item.pool_idx, 2u);
    EXPECT_EQ(item.height, 100ull);
    EXPECT_EQ(item.priority, 3u);
    EXPECT_EQ(item.tag, kBlockHeight);
    EXPECT_EQ(item.sync_times, 0u);
    EXPECT_EQ(item.sync_tm_us, 0ull);
    EXPECT_EQ(item.responsed_timeout_us, common::kInvalidUint64);
    std::string expected_key = "h_10_2_100";
    EXPECT_EQ(item.key, expected_key);
}

TEST(SyncKvBranches, SyncItemViewHashConstructor) {
    SyncItem item(5u, "my_view_hash", 2u);
    EXPECT_EQ(item.network_id, 5u);
    EXPECT_EQ(item.key, "my_view_hash");
    EXPECT_EQ(item.priority, 2u);
    EXPECT_EQ(item.tag, kViewHash);
    EXPECT_EQ(item.sync_tm_us, 0ull);
    EXPECT_EQ(item.sync_times, 0u);
    EXPECT_EQ(item.responsed_timeout_us, common::kInvalidUint64);
}

TEST(SyncKvBranches, SyncItemBlockViewTag) {
    SyncItem item(1u, 0u, 999ull, 4u, kBlockView);
    EXPECT_EQ(item.tag, kBlockView);
    std::string expected_key = "w_1_0_999";
    EXPECT_EQ(item.key, expected_key);
}

TEST(SyncKvBranches, KeyValueSyncConstructDestruct) {
    KeyValueSync kvs;
    EXPECT_FALSE(kvs.destroy_.load());
}

TEST(SyncKvBranches, OnNewElectBlockUpdatesHeight) {
    KeyValueSync kvs;
    uint32_t shard = network::kConsensusShardBeginNetworkId;
    kvs.elect_net_heights_map_[shard] = 0;
    kvs.OnNewElectBlock(shard, 100);
    EXPECT_EQ(kvs.elect_net_heights_map_[shard], 100ull);
}

TEST(SyncKvBranches, OnNewElectBlockNoUpdateWhenLower) {
    KeyValueSync kvs;
    uint32_t shard = network::kConsensusShardBeginNetworkId;
    kvs.elect_net_heights_map_[shard] = 200;
    kvs.OnNewElectBlock(shard, 100);
    EXPECT_EQ(kvs.elect_net_heights_map_[shard], 200ull);
}

TEST(SyncKvBranches, OnNewElectBlockUpdatesMaxShardingId) {
    KeyValueSync kvs;
    uint32_t shard = kvs.max_sharding_id_ + 1;
    if (shard < network::kConsensusShardEndNetworkId) {
        kvs.elect_net_heights_map_[shard] = 0;
        uint32_t old_max = kvs.max_sharding_id_;
        kvs.OnNewElectBlock(shard, 50);
        EXPECT_GT(kvs.max_sharding_id_, old_max);
    }
}

TEST(SyncKvBranches, OnNewElectBlockNoMaxUpdateWhenSmaller) {
    KeyValueSync kvs;
    uint32_t shard = network::kConsensusShardBeginNetworkId;
    kvs.max_sharding_id_ = shard + 5;
    kvs.elect_net_heights_map_[shard] = 0;
    kvs.OnNewElectBlock(shard, 50);
    EXPECT_EQ(kvs.max_sharding_id_, shard + 5);
}

TEST(SyncKvBranches, FirewallCheckAlwaysSuccess) {
    KeyValueSync kvs;
    transport::MessagePtr msg;
    EXPECT_EQ(kvs.FirewallCheckMessage(msg), transport::kFirewallCheckSuccess);
}

TEST(SyncKvBranches, StaticConstants) {
    EXPECT_GT(KeyValueSync::kSyncPeriodUs, 0ull);
    EXPECT_GT(KeyValueSync::kSyncTimeoutPeriodUs, 0ull);
    EXPECT_GT(KeyValueSync::kEachTimerHandleCount, 0u);
    EXPECT_GT(KeyValueSync::kMaxBatchDrainCount, 0u);
    EXPECT_GT(KeyValueSync::kCacheSyncKeyValueCount, 0u);
    EXPECT_GT(KeyValueSync::kConsumerBatchSize, 0u);
}

}  // namespace test
}  // namespace sync
}  // namespace shardora
