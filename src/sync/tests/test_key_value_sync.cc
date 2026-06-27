#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include "common/random.h"
#include "common/hash.h"
#include "common/time_utils.h"
#include "sync/sync_utils.h"
#include "protos/sync.pb.h"
#include "protos/view_block.pb.h"

#define private public
#include "sync/key_value_sync.h"

namespace shardora {

namespace sync {

namespace test {

class TestKeyValueSync : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

// --- SyncItem Tests ---

TEST_F(TestKeyValueSync, SyncItemConstructWithKey) {
    uint32_t net_id = 3;
    std::string key = "test_sync_key";
    uint32_t priority = kSyncHigh;

    SyncItem item(net_id, key, priority);
    ASSERT_EQ(item.network_id, net_id);
    ASSERT_EQ(item.key, key);
    ASSERT_EQ(item.priority, priority);
    ASSERT_EQ(item.sync_times, 0u);
    ASSERT_EQ(item.tag, (uint32_t)kViewHash);
    ASSERT_EQ(item.sync_tm_us, 0u);
    ASSERT_EQ(item.responsed_timeout_us, common::kInvalidUint64);
}

TEST_F(TestKeyValueSync, SyncItemConstructWithHeight) {
    uint32_t net_id = 3;
    uint32_t pool_idx = 5;
    uint64_t height = 1000;
    uint32_t priority = kSyncNormal;

    SyncItem item(net_id, pool_idx, height, priority, kBlockHeight);
    ASSERT_EQ(item.network_id, net_id);
    ASSERT_EQ(item.pool_idx, pool_idx);
    ASSERT_EQ(item.height, height);
    ASSERT_EQ(item.priority, priority);
    ASSERT_EQ(item.tag, (uint32_t)kBlockHeight);
    ASSERT_EQ(item.sync_times, 0u);
    // Key should be constructed from net_id, pool_idx, height, tag
    ASSERT_FALSE(item.key.empty());
    ASSERT_NE(item.key.find(std::to_string(net_id)), std::string::npos);
    ASSERT_NE(item.key.find(std::to_string(pool_idx)), std::string::npos);
    ASSERT_NE(item.key.find(std::to_string(height)), std::string::npos);
}

TEST_F(TestKeyValueSync, SyncItemConstructWithView) {
    uint32_t net_id = 4;
    uint32_t pool_idx = 10;
    uint64_t height = 500;
    uint32_t priority = kSyncHighest;

    SyncItem item(net_id, pool_idx, height, priority, kBlockView);
    ASSERT_EQ(item.tag, (uint32_t)kBlockView);
    ASSERT_EQ(item.network_id, net_id);
    ASSERT_EQ(item.pool_idx, pool_idx);
    ASSERT_EQ(item.height, height);
}

TEST_F(TestKeyValueSync, SyncItemKeyUniqueness) {
    // Different heights should produce different keys
    SyncItem item1(3, 0, 100, kSyncNormal, kBlockHeight);
    SyncItem item2(3, 0, 101, kSyncNormal, kBlockHeight);
    ASSERT_NE(item1.key, item2.key);

    // Different pools should produce different keys
    SyncItem item3(3, 0, 100, kSyncNormal, kBlockHeight);
    SyncItem item4(3, 1, 100, kSyncNormal, kBlockHeight);
    ASSERT_NE(item3.key, item4.key);

    // Different networks should produce different keys
    SyncItem item5(3, 0, 100, kSyncNormal, kBlockHeight);
    SyncItem item6(4, 0, 100, kSyncNormal, kBlockHeight);
    ASSERT_NE(item5.key, item6.key);

    // Different tags should produce different keys
    SyncItem item7(3, 0, 100, kSyncNormal, kBlockHeight);
    SyncItem item8(3, 0, 100, kSyncNormal, kBlockView);
    ASSERT_NE(item7.key, item8.key);
}

TEST_F(TestKeyValueSync, SyncItemPriorityLevels) {
    SyncItem lowest(3, "k1", kSyncPriLowest);
    SyncItem low(3, "k2", kSyncPriLow);
    SyncItem normal(3, "k3", kSyncNormal);
    SyncItem high(3, "k4", kSyncHigh);
    SyncItem highest(3, "k5", kSyncHighest);

    ASSERT_EQ(lowest.priority, 0u);
    ASSERT_EQ(low.priority, 1u);
    ASSERT_EQ(normal.priority, 2u);
    ASSERT_EQ(high.priority, 3u);
    ASSERT_EQ(highest.priority, 4u);
}

// --- Sync Constants Tests ---

TEST_F(TestKeyValueSync, SyncConstants) {
    // Verify key constants are reasonable
    ASSERT_GT(kSyncValueRetryPeriod, 0u);
    ASSERT_GT(kTimeoutCheckPeriod, 0u);
    ASSERT_GT(kMaxSyncMapCapacity, 0u);
    ASSERT_GT(kEachRequestMaxSyncKeyCount, 0u);
    ASSERT_GT(kSyncNeighborCount, 0u);
    ASSERT_GT(kSyncTickPeriod, 0u);
    ASSERT_GT(kSyncPacketMaxSize, 0u);
    ASSERT_GT(kSyncMaxKeyCount, 0u);
    ASSERT_GT(kSyncMaxRetryTimes, 0u);
    ASSERT_GT(kPoolHeightPairCount, 0u);

    // Verify relationships
    ASSERT_LE(kEachRequestMaxSyncKeyCount, kSyncMaxKeyCount);
    ASSERT_LT(kSyncPacketMaxSize, 1024u * 1024u);  // Should be less than 1MB
}

TEST_F(TestKeyValueSync, SyncErrorCodes) {
    ASSERT_EQ(kSyncSuccess, 0);
    ASSERT_NE(kSyncError, kSyncSuccess);
    ASSERT_NE(kSyncKeyExsits, kSyncSuccess);
    ASSERT_NE(kSyncKeyAdded, kSyncSuccess);
    ASSERT_NE(kSyncBlockReloaded, kSyncSuccess);
}

TEST_F(TestKeyValueSync, SyncPriorityTypes) {
    ASSERT_LT(kSyncPriLowest, kSyncPriLow);
    ASSERT_LT(kSyncPriLow, kSyncNormal);
    ASSERT_LT(kSyncNormal, kSyncHigh);
    ASSERT_LT(kSyncHigh, kSyncHighest);
}

// --- Sync Protobuf Message Tests ---

TEST_F(TestKeyValueSync, SyncRequestMessage) {
    sync::protobuf::SyncMessage sync_msg;
    auto* req = sync_msg.mutable_sync_value_req();
    req->set_network_id(3);
    req->add_keys("key1");
    req->add_keys("key2");

    auto* height_item = req->add_heights();
    height_item->set_pool_idx(0);
    height_item->set_height(100);
    height_item->set_tag(kBlockHeight);

    std::string serialized = sync_msg.SerializeAsString();
    ASSERT_FALSE(serialized.empty());

    sync::protobuf::SyncMessage deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    ASSERT_TRUE(deserialized.has_sync_value_req());
    ASSERT_EQ(deserialized.sync_value_req().network_id(), 3u);
    ASSERT_EQ(deserialized.sync_value_req().keys_size(), 2);
    ASSERT_EQ(deserialized.sync_value_req().keys(0), "key1");
    ASSERT_EQ(deserialized.sync_value_req().keys(1), "key2");
    ASSERT_EQ(deserialized.sync_value_req().heights_size(), 1);
    ASSERT_EQ(deserialized.sync_value_req().heights(0).pool_idx(), 0u);
    ASSERT_EQ(deserialized.sync_value_req().heights(0).height(), 100u);
    ASSERT_EQ(deserialized.sync_value_req().heights(0).tag(), (uint32_t)kBlockHeight);
}

TEST_F(TestKeyValueSync, SyncResponseMessage) {
    sync::protobuf::SyncMessage sync_msg;
    auto* res = sync_msg.mutable_sync_value_res();

    auto* item = res->add_res();
    item->set_network_id(3);
    item->set_pool_idx(5);
    item->set_height(200);
    item->set_value("serialized_view_block_data");
    item->set_key("sync_key_1");
    item->set_tag(kBlockHeight);

    std::string serialized = sync_msg.SerializeAsString();
    sync::protobuf::SyncMessage deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    ASSERT_TRUE(deserialized.has_sync_value_res());
    ASSERT_EQ(deserialized.sync_value_res().res_size(), 1);
    ASSERT_EQ(deserialized.sync_value_res().res(0).network_id(), 3u);
    ASSERT_EQ(deserialized.sync_value_res().res(0).pool_idx(), 5u);
    ASSERT_EQ(deserialized.sync_value_res().res(0).height(), 200u);
    ASSERT_EQ(deserialized.sync_value_res().res(0).value(), "serialized_view_block_data");
    ASSERT_EQ(deserialized.sync_value_res().res(0).tag(), (uint32_t)kBlockHeight);
}

TEST_F(TestKeyValueSync, SyncRequestWithLatestSyncItem) {
    sync::protobuf::SyncMessage sync_msg;
    auto* req = sync_msg.mutable_sync_value_req();
    req->set_network_id(3);

    auto* latest = req->mutable_latest_sync_item();
    latest->set_network_id(3);
    latest->set_globl_pool_height(500);
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        latest->add_pool_latest_heights(100 + i);
    }

    std::string serialized = sync_msg.SerializeAsString();
    sync::protobuf::SyncMessage deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    ASSERT_TRUE(deserialized.sync_value_req().has_latest_sync_item());
    ASSERT_EQ(deserialized.sync_value_req().latest_sync_item().network_id(), 3u);
    ASSERT_EQ(deserialized.sync_value_req().latest_sync_item().globl_pool_height(), 500u);
    ASSERT_EQ(deserialized.sync_value_req().latest_sync_item().pool_latest_heights_size(),
              (int)common::kImmutablePoolSize);
}

TEST_F(TestKeyValueSync, SyncMultipleHeightItems) {
    sync::protobuf::SyncMessage sync_msg;
    auto* req = sync_msg.mutable_sync_value_req();
    req->set_network_id(3);

    // Add multiple height requests
    for (uint32_t pool = 0; pool < 10; ++pool) {
        for (uint64_t h = 0; h < 5; ++h) {
            auto* item = req->add_heights();
            item->set_pool_idx(pool);
            item->set_height(h);
            item->set_tag(kBlockHeight);
        }
    }

    ASSERT_EQ(req->heights_size(), 50);

    std::string serialized = sync_msg.SerializeAsString();
    sync::protobuf::SyncMessage deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    ASSERT_EQ(deserialized.sync_value_req().heights_size(), 50);
}

// --- ViewBlock Protobuf Tests ---

TEST_F(TestKeyValueSync, ViewBlockItemSerialize) {
    view_block::protobuf::ViewBlockItem vblock;
    auto* qc = vblock.mutable_qc();
    qc->set_network_id(3);
    qc->set_pool_index(7);
    qc->set_view(100);
    qc->set_view_block_hash("hash_32_bytes_placeholder_data!!");
    qc->set_sign_x("sign_x_data");
    qc->set_sign_y("sign_y_data");

    auto* block_info = vblock.mutable_block_info();
    block_info->set_height(50);
    block_info->set_timestamp(1700000000000);

    std::string serialized = vblock.SerializeAsString();
    ASSERT_FALSE(serialized.empty());

    view_block::protobuf::ViewBlockItem deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    ASSERT_EQ(deserialized.qc().network_id(), 3u);
    ASSERT_EQ(deserialized.qc().pool_index(), 7u);
    ASSERT_EQ(deserialized.qc().view(), 100u);
    ASSERT_EQ(deserialized.block_info().height(), 50u);
    ASSERT_EQ(deserialized.block_info().timestamp(), 1700000000000u);
}

// --- SyncItemTag Tests ---

TEST_F(TestKeyValueSync, SyncItemTagValues) {
    ASSERT_EQ(kBlockHeight, 1u);
    ASSERT_EQ(kViewHash, 2u);
    ASSERT_EQ(kBlockView, 3u);
    // All tags should be distinct
    ASSERT_NE(kBlockHeight, kViewHash);
    ASSERT_NE(kBlockHeight, kBlockView);
    ASSERT_NE(kViewHash, kBlockView);
}

}  // namespace test

}  // namespace sync

}  // namespace shardora
