#include <stdlib.h>

#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/random.h"
#include "common/hash.h"
#include "common/time_utils.h"
#include "protos/vss.pb.h"
#include "protos/view_block.pb.h"

#define private public
#include "vss/vss_manager.h"
#include "vss/vss_utils.h"
#include "vss/random_num.h"

namespace shardora {

namespace vss {

namespace test {

class TestVssManager : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}

protected:
    // Helper to create a ViewBlockItem with QC signatures
    std::shared_ptr<view_block::protobuf::ViewBlockItem> MakeViewBlock(
            const std::string& sign_x, const std::string& sign_y) {
        auto block = std::make_shared<view_block::protobuf::ViewBlockItem>();
        auto* qc = block->mutable_qc();
        qc->set_sign_x(sign_x);
        qc->set_sign_y(sign_y);
        qc->set_network_id(3);
        qc->set_pool_index(32);  // global pool
        qc->set_view(100);
        auto* block_info = block->mutable_block_info();
        block_info->set_height(50);
        block_info->set_timestamp(common::TimeUtils::TimestampMs());
        return block;
    }
};

// --- VssManager Basic Tests ---

TEST_F(TestVssManager, DefaultConstruction) {
    VssManager mgr;
    ASSERT_EQ(mgr.EpochRandom(), 0u);
    ASSERT_EQ(mgr.GetConsensusFinalRandom(), 0u);
}

TEST_F(TestVssManager, OnTimeBlockUpdatesRandom) {
    VssManager mgr;
    auto block = MakeViewBlock("sign_x_data_abc", "sign_y_data_xyz");
    mgr.OnTimeBlock(block);

    uint64_t expected = common::Hash::Hash64(
        block->qc().sign_x() + block->qc().sign_y());
    ASSERT_EQ(mgr.EpochRandom(), expected);
    ASSERT_EQ(mgr.GetConsensusFinalRandom(), expected);
    ASSERT_NE(mgr.EpochRandom(), 0u);
}

TEST_F(TestVssManager, OnTimeBlockDeterministic) {
    VssManager mgr1, mgr2;
    auto block = MakeViewBlock("same_sign_x", "same_sign_y");
    mgr1.OnTimeBlock(block);
    mgr2.OnTimeBlock(block);

    ASSERT_EQ(mgr1.EpochRandom(), mgr2.EpochRandom());
}

TEST_F(TestVssManager, OnTimeBlockDifferentSignatures) {
    VssManager mgr1, mgr2;
    auto block1 = MakeViewBlock("sign_x_A", "sign_y_A");
    auto block2 = MakeViewBlock("sign_x_B", "sign_y_B");
    mgr1.OnTimeBlock(block1);
    mgr2.OnTimeBlock(block2);

    ASSERT_NE(mgr1.EpochRandom(), mgr2.EpochRandom());
}

TEST_F(TestVssManager, OnTimeBlockOverwritesPrevious) {
    VssManager mgr;
    auto block1 = MakeViewBlock("first_x", "first_y");
    mgr.OnTimeBlock(block1);
    uint64_t first_random = mgr.EpochRandom();

    auto block2 = MakeViewBlock("second_x", "second_y");
    mgr.OnTimeBlock(block2);
    uint64_t second_random = mgr.EpochRandom();

    ASSERT_NE(first_random, second_random);
    ASSERT_EQ(mgr.EpochRandom(), second_random);
}

TEST_F(TestVssManager, EpochRandomEqualsGetConsensusFinalRandom) {
    VssManager mgr;
    auto block = MakeViewBlock("x_data", "y_data");
    mgr.OnTimeBlock(block);
    ASSERT_EQ(mgr.EpochRandom(), mgr.GetConsensusFinalRandom());
}

// --- RandomNum Tests ---

TEST_F(TestVssManager, RandomNumDefaultState) {
    RandomNum rn;
    ASSERT_FALSE(rn.IsRandomValid());
    ASSERT_FALSE(rn.IsRandomInvalid());
    ASSERT_EQ(rn.GetHash(), 0u);
    ASSERT_EQ(rn.GetFinalRandomNum(), 0u);
}

TEST_F(TestVssManager, RandomNumLocalOnTimeBlock) {
    RandomNum rn(true);  // is_local = true
    uint64_t tm = common::TimeUtils::TimestampSeconds();
    rn.OnTimeBlock(tm);

    ASSERT_TRUE(rn.IsRandomValid());
    ASSERT_NE(rn.GetFinalRandomNum(), 0u);
    ASSERT_NE(rn.GetHash(), 0u);
    // Hash should be Hash64 of the random number string
    ASSERT_EQ(rn.GetHash(), common::Hash::Hash64(std::to_string(rn.GetFinalRandomNum())));
}

TEST_F(TestVssManager, RandomNumLocalOnTimeBlockIdempotent) {
    RandomNum rn(true);
    uint64_t tm = 1000;
    rn.OnTimeBlock(tm);
    uint64_t first_random = rn.GetFinalRandomNum();
    uint64_t first_hash = rn.GetHash();

    // Same timestamp should not re-initialize
    rn.OnTimeBlock(tm);
    ASSERT_EQ(rn.GetFinalRandomNum(), first_random);
    ASSERT_EQ(rn.GetHash(), first_hash);
}

TEST_F(TestVssManager, RandomNumLocalOnTimeBlockNewTimestamp) {
    RandomNum rn(true);
    rn.OnTimeBlock(1000);
    uint64_t first_random = rn.GetFinalRandomNum();

    // New (higher) timestamp should re-initialize
    rn.OnTimeBlock(2000);
    // After re-init, it generates a new random (may or may not differ, but state is reset)
    ASSERT_TRUE(rn.IsRandomValid());
    ASSERT_NE(rn.GetHash(), 0u);
}

TEST_F(TestVssManager, RandomNumRemoteSetHash) {
    RandomNum rn(false);  // is_local = false
    std::string owner_id = "node_123";
    uint64_t hash_val = 0xDEADBEEF;

    rn.Sethash(owner_id, hash_val);
    ASSERT_EQ(rn.GetHash(), hash_val);
    ASSERT_FALSE(rn.IsRandomValid());  // Not valid until final random is set
}

TEST_F(TestVssManager, RandomNumRemoteSetFinalRandom) {
    RandomNum rn(false);
    std::string owner_id = "node_456";
    uint64_t final_random = 12345678;
    uint64_t hash_val = common::Hash::Hash64(std::to_string(final_random));

    // First set hash
    rn.Sethash(owner_id, hash_val);
    ASSERT_FALSE(rn.IsRandomValid());

    // Then set final random (must match hash)
    rn.SetFinalRandomNum(owner_id, final_random);
    ASSERT_TRUE(rn.IsRandomValid());
    ASSERT_EQ(rn.GetFinalRandomNum(), final_random);
}

TEST_F(TestVssManager, RandomNumRemoteSetFinalRandomWrongHash) {
    RandomNum rn(false);
    std::string owner_id = "node_789";
    uint64_t final_random = 12345678;
    uint64_t wrong_hash = 0xBADBAD;  // Doesn't match

    rn.Sethash(owner_id, wrong_hash);
    rn.SetFinalRandomNum(owner_id, final_random);
    // Should NOT become valid because hash doesn't match
    ASSERT_FALSE(rn.IsRandomValid());
    ASSERT_EQ(rn.GetFinalRandomNum(), 0u);
}

TEST_F(TestVssManager, RandomNumRemoteSetFinalRandomWrongOwner) {
    RandomNum rn(false);
    std::string owner_id = "node_A";
    std::string wrong_owner = "node_B";
    uint64_t final_random = 99999;
    uint64_t hash_val = common::Hash::Hash64(std::to_string(final_random));

    rn.Sethash(owner_id, hash_val);
    // Wrong owner tries to set final random
    rn.SetFinalRandomNum(wrong_owner, final_random);
    ASSERT_FALSE(rn.IsRandomValid());
}

TEST_F(TestVssManager, RandomNumResetStatus) {
    RandomNum rn(true);
    rn.OnTimeBlock(1000);
    ASSERT_TRUE(rn.IsRandomValid());

    rn.ResetStatus();
    ASSERT_FALSE(rn.IsRandomValid());
    ASSERT_EQ(rn.GetHash(), 0u);
    ASSERT_EQ(rn.GetFinalRandomNum(), 0u);
}

// --- VSS Constants Tests ---

TEST_F(TestVssManager, VssConstants) {
    ASSERT_GT(kVssRandomSplitCount, 0);
    ASSERT_GT(kVssRandomduplicationCount, 0u);
    ASSERT_GT(kVssCheckPeriodTimeout, 0);
    ASSERT_GT(kVssTimePeriodOffsetSeconds, 0u);
    ASSERT_GT(kHandleMessageVssTimePeriodOffsetSeconds, 0u);
}

TEST_F(TestVssManager, VssErrorCodes) {
    ASSERT_EQ(kVssSuccess, 0);
    ASSERT_NE(kVssError, kVssSuccess);
}

TEST_F(TestVssManager, VssMessageTypes) {
    ASSERT_EQ(kVssRandomHash, 1);
    ASSERT_EQ(kVssRandom, 2);
    ASSERT_EQ(kVssFinalRandom, 3);
    // All distinct
    ASSERT_NE(kVssRandomHash, kVssRandom);
    ASSERT_NE(kVssRandom, kVssFinalRandom);
}

// --- ElectItem Tests ---

TEST_F(TestVssManager, ElectItemDefault) {
    ElectItem item;
    ASSERT_EQ(item.members, nullptr);
    ASSERT_EQ(item.local_index, elect::kInvalidMemberIndex);
    ASSERT_EQ(item.member_count, 0u);
    ASSERT_EQ(item.elect_height, 0u);
    ASSERT_FALSE(item.this_node_is_leader);
}

// --- VSS Protobuf Tests ---

TEST_F(TestVssManager, VssMessageProtoSerialize) {
    vss::protobuf::VssMessage msg;
    msg.set_member_index(5);
    msg.set_tm_height(100);
    msg.set_elect_height(50);
    msg.set_type(kVssRandomHash);
    msg.set_random_hash(0xCAFEBABE);

    std::string serialized = msg.SerializeAsString();
    ASSERT_FALSE(serialized.empty());

    vss::protobuf::VssMessage deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    ASSERT_EQ(deserialized.member_index(), 5u);
    ASSERT_EQ(deserialized.tm_height(), 100u);
    ASSERT_EQ(deserialized.elect_height(), 50u);
    ASSERT_EQ(deserialized.type(), kVssRandomHash);
    ASSERT_EQ(deserialized.random_hash(), 0xCAFEBABEu);
}

TEST_F(TestVssManager, VssMessageWithRandom) {
    vss::protobuf::VssMessage msg;
    msg.set_type(kVssRandom);
    msg.set_random(0xDEADBEEF);
    msg.set_member_index(10);
    msg.set_tm_height(200);
    msg.set_elect_height(100);

    std::string serialized = msg.SerializeAsString();
    vss::protobuf::VssMessage deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    ASSERT_EQ(deserialized.type(), kVssRandom);
    ASSERT_EQ(deserialized.random(), 0xDEADBEEFu);
}

// --- XOR Commutativity Property ---

TEST_F(TestVssManager, XorCommutativity) {
    // VSS relies on XOR being commutative and associative
    std::vector<uint64_t> randoms;
    for (int i = 0; i < 100; ++i) {
        randoms.push_back(common::Random::RandomUint64());
    }

    uint64_t xor_result = 0;
    for (auto r : randoms) {
        xor_result ^= r;
    }

    // Shuffle and XOR again - should get same result
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(randoms.begin(), randoms.end(), g);

    uint64_t xor_shuffled = 0;
    for (auto r : randoms) {
        xor_shuffled ^= r;
    }

    ASSERT_EQ(xor_result, xor_shuffled);
}

}  // namespace test

}  // namespace vss

}  // namespace shardora
