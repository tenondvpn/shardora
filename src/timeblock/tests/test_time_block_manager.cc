#include <stdlib.h>

#include <iostream>
#include <string>

#include <gtest/gtest.h>

#include "common/time_utils.h"
#include "common/utils.h"
#include "protos/timeblock.pb.h"

#define private public
#include "timeblock/time_block_manager.h"
#include "timeblock/time_block_utils.h"

namespace shardora {

namespace timeblock {

namespace test {

class TestTimeBlockManager : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

// --- Construction and Initial State ---

TEST_F(TestTimeBlockManager, DefaultConstruction) {
    TimeBlockManager mgr;
    ASSERT_EQ(mgr.LatestTimestamp(), 0u);
    ASSERT_EQ(mgr.LatestTimestampHeight(), common::kInvalidUint64);
    ASSERT_EQ(mgr.LatestPrevTimestampHeight(), common::kInvalidUint64);
}

// --- OnTimeBlock Tests ---

TEST_F(TestTimeBlockManager, OnTimeBlockFirstBlock) {
    TimeBlockManager mgr;
    uint64_t tm = common::TimeUtils::TimestampSeconds();
    uint64_t height = 1;
    uint64_t vss_random = 12345;

    mgr.OnTimeBlock(tm, height, vss_random);
    ASSERT_EQ(mgr.LatestTimestamp(), tm);
    ASSERT_EQ(mgr.LatestTimestampHeight(), height);
    ASSERT_EQ(mgr.LatestPrevTimestampHeight(), height);
}

TEST_F(TestTimeBlockManager, OnTimeBlockIncreasingHeight) {
    TimeBlockManager mgr;
    uint64_t tm1 = 1000;
    uint64_t tm2 = 1600;  // 600s later (one period)

    mgr.OnTimeBlock(tm1, 1, 111);
    ASSERT_EQ(mgr.LatestTimestampHeight(), 1u);

    mgr.OnTimeBlock(tm2, 2, 222);
    ASSERT_EQ(mgr.LatestTimestampHeight(), 2u);
    ASSERT_EQ(mgr.LatestTimestamp(), tm2);
    ASSERT_EQ(mgr.LatestPrevTimestampHeight(), 2u);
}

TEST_F(TestTimeBlockManager, OnTimeBlockSameHeightIgnored) {
    TimeBlockManager mgr;
    mgr.OnTimeBlock(1000, 5, 111);
    ASSERT_EQ(mgr.LatestTimestamp(), 1000u);
    ASSERT_EQ(mgr.LatestTimestampHeight(), 5u);

    // Same height should be ignored (no update to latest)
    mgr.OnTimeBlock(2000, 5, 222);
    ASSERT_EQ(mgr.LatestTimestamp(), 1000u);  // Not updated
    ASSERT_EQ(mgr.LatestTimestampHeight(), 5u);
}

TEST_F(TestTimeBlockManager, OnTimeBlockLowerHeightIgnored) {
    TimeBlockManager mgr;
    mgr.OnTimeBlock(2000, 10, 111);
    ASSERT_EQ(mgr.LatestTimestampHeight(), 10u);

    // Lower height should be ignored
    mgr.OnTimeBlock(1000, 5, 222);
    ASSERT_EQ(mgr.LatestTimestamp(), 2000u);  // Not updated
    ASSERT_EQ(mgr.LatestTimestampHeight(), 10u);
}

TEST_F(TestTimeBlockManager, OnTimeBlockPrevHeightUpdated) {
    TimeBlockManager mgr;
    mgr.OnTimeBlock(1000, 1, 111);
    mgr.OnTimeBlock(2000, 5, 222);
    ASSERT_EQ(mgr.LatestPrevTimestampHeight(), 5u);

    // Even if height is same/lower, prev_time_block_height_ can be updated
    // if it's greater than current prev
    mgr.OnTimeBlock(1500, 3, 333);
    // prev should still be 5 (from the last successful update)
    ASSERT_EQ(mgr.LatestPrevTimestampHeight(), 5u);
}

// --- CanCallTimeBlockTx Tests ---

TEST_F(TestTimeBlockManager, CanCallTimeBlockTxInitially) {
    TimeBlockManager mgr;
    mgr.latest_time_block_tm_ = 0;
    mgr.latest_tm_block_local_sec_ = 0;
    // With tm=0, current time is always >= 0 + period
    ASSERT_TRUE(mgr.CanCallTimeBlockTx());
}

TEST_F(TestTimeBlockManager, CanCallTimeBlockTxTooEarly) {
    TimeBlockManager mgr;
    uint64_t now = common::TimeUtils::TimestampSeconds();
    mgr.latest_time_block_tm_ = now;  // Just happened
    mgr.latest_tm_block_local_sec_ = now;
    // Should not be callable yet (need to wait kTimeBlockCreatePeriodSeconds)
    ASSERT_FALSE(mgr.CanCallTimeBlockTx());
}

TEST_F(TestTimeBlockManager, CanCallTimeBlockTxAfterPeriod) {
    TimeBlockManager mgr;
    uint64_t now = common::TimeUtils::TimestampSeconds();
    // Set time to one full period ago
    mgr.latest_time_block_tm_ = now - common::kTimeBlockCreatePeriodSeconds;
    mgr.latest_tm_block_local_sec_ = now - common::kTimeBlockCreatePeriodSeconds;
    ASSERT_TRUE(mgr.CanCallTimeBlockTx());
}

TEST_F(TestTimeBlockManager, CanCallTimeBlockTxLocalTimeOverride) {
    TimeBlockManager mgr;
    uint64_t now = common::TimeUtils::TimestampSeconds();
    // latest_time_block_tm_ is recent (not ready)
    mgr.latest_time_block_tm_ = now;
    // But local sec is old enough
    mgr.latest_tm_block_local_sec_ = now - common::kTimeBlockCreatePeriodSeconds;
    // Should be callable because local time condition is met
    ASSERT_TRUE(mgr.CanCallTimeBlockTx());
}

// --- TimeBlock Protobuf Tests ---

TEST_F(TestTimeBlockManager, TimeBlockProtoSerialize) {
    timeblock::protobuf::TimeBlock tb;
    tb.set_timestamp(1700000000);
    tb.set_vss_random(0xDEADBEEF);
    tb.set_nonce(42);

    std::string serialized = tb.SerializeAsString();
    ASSERT_FALSE(serialized.empty());

    timeblock::protobuf::TimeBlock deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    ASSERT_EQ(deserialized.timestamp(), 1700000000u);
    ASSERT_EQ(deserialized.vss_random(), 0xDEADBEEFu);
    ASSERT_EQ(deserialized.nonce(), 42u);
}

// --- Constants Tests ---

TEST_F(TestTimeBlockManager, ConstantsValid) {
    ASSERT_GT(kTimeBlockTolerateSeconds, 0u);
    ASSERT_GT(kTimeBlockMaxOffsetSeconds, 0u);
    ASSERT_GT(kTimeBlockAvgCount, 0u);
    ASSERT_GT(kCheckTimeBlockPeriodUs, 0u);
    ASSERT_GT(kCheckBftPeriodUs, 0u);
    ASSERT_GT(common::kTimeBlockCreatePeriodSeconds, 0u);
}

TEST_F(TestTimeBlockManager, ErrorCodes) {
    ASSERT_EQ(kTimeBlockSuccess, 0);
    ASSERT_NE(kTimeBlockError, kTimeBlockSuccess);
    ASSERT_NE(kTimeBlockVssError, kTimeBlockSuccess);
    ASSERT_NE(kTimeBlockError, kTimeBlockVssError);
}

// --- Multiple OnTimeBlock Sequence ---

TEST_F(TestTimeBlockManager, OnTimeBlockSequence) {
    TimeBlockManager mgr;
    uint64_t base_tm = 1000;
    uint64_t period = common::kTimeBlockCreatePeriodSeconds;

    for (uint64_t i = 1; i <= 10; ++i) {
        mgr.OnTimeBlock(base_tm + i * period, i, i * 100);
        ASSERT_EQ(mgr.LatestTimestampHeight(), i);
        ASSERT_EQ(mgr.LatestTimestamp(), base_tm + i * period);
    }

    ASSERT_EQ(mgr.LatestTimestampHeight(), 10u);
    ASSERT_EQ(mgr.LatestPrevTimestampHeight(), 10u);
}

TEST_F(TestTimeBlockManager, OnTimeBlockNonSequentialHeights) {
    TimeBlockManager mgr;
    // Heights arrive out of order (can happen during sync)
    mgr.OnTimeBlock(1000, 5, 111);
    ASSERT_EQ(mgr.LatestTimestampHeight(), 5u);

    mgr.OnTimeBlock(2000, 10, 222);
    ASSERT_EQ(mgr.LatestTimestampHeight(), 10u);

    // Height 7 arrives late - should be ignored for latest but update prev
    mgr.OnTimeBlock(1500, 7, 333);
    ASSERT_EQ(mgr.LatestTimestampHeight(), 10u);  // Still 10
    ASSERT_EQ(mgr.LatestTimestamp(), 2000u);  // Still 2000
}

}  // namespace test

}  // namespace timeblock

}  // namespace shardora
