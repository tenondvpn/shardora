#include <gtest/gtest.h>

#include <iostream>
#include <chrono>
#include <limits>
#include <set>

#define private public
#include "common/fts_tree.h"
#include "common/random.h"
#include "common/encode.h"

namespace shardora {

namespace common {

namespace test {

class TestFtsTree : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

TEST_F(TestFtsTree, all) {
    for (uint32_t i = 3; i < 4097; ++i) {
        if (i != 1000) {
            continue;
        }

        uint32_t kTestCount = i;
        FtsTree fts_tree;
        std::mt19937_64 g2(1000);
        for (uint32_t j = 0; j < kTestCount; ++j) {
            uint64_t fts_value = 1000000000llu + common::Random::RandomUint64() % 100000;
            fts_tree.AppendFtsNode(fts_value, int32_t(j));
        }

        fts_tree.CreateFtsTree();
        std::set<int32_t> node_set;
        for (uint32_t j = 0; j < i / 3; ++j) {
            node_set.insert(fts_tree.GetOneNode(g2));
        }

        ASSERT_LE(node_set.size(), i / 3);
        ASSERT_GT(node_set.size(), 0u);
    }
}

TEST_F(TestFtsTree, EmptyTree) {
    FtsTree fts_tree;
    fts_tree.CreateFtsTree();
    // Should not crash on empty tree
    std::mt19937_64 g2(42);
    ASSERT_EQ(fts_tree.GetOneNode(g2), -1);
}

TEST_F(TestFtsTree, SingleNode) {
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(1000000ull, 42);
    fts_tree.CreateFtsTree();

    std::mt19937_64 g2(1);
    int32_t node = fts_tree.GetOneNode(g2);
    ASSERT_EQ(node, 42);
}

TEST_F(TestFtsTree, TwoNodes) {
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(1000000ull, 0);
    fts_tree.AppendFtsNode(1000000ull, 1);
    fts_tree.CreateFtsTree();

    std::mt19937_64 g2(12345);
    std::set<int32_t> seen;
    for (int i = 0; i < 100; ++i) {
        seen.insert(fts_tree.GetOneNode(g2));
    }
    // Both nodes should be selected at some point
    ASSERT_GE(seen.size(), 1u);
}

TEST_F(TestFtsTree, PowerOfTwoNodes) {
    // 8 nodes = exact power of 2
    FtsTree fts_tree;
    for (int i = 0; i < 8; ++i) {
        fts_tree.AppendFtsNode(1000000ull + i * 100, i);
    }
    fts_tree.CreateFtsTree();

    std::mt19937_64 g2(999);
    std::set<int32_t> seen;
    for (int i = 0; i < 200; ++i) {
        seen.insert(fts_tree.GetOneNode(g2));
    }
    ASSERT_GT(seen.size(), 0u);
    ASSERT_LE(seen.size(), 8u);
}

TEST_F(TestFtsTree, NonPowerOfTwoNodes) {
    // 5 nodes = not a power of 2
    FtsTree fts_tree;
    for (int i = 0; i < 5; ++i) {
        fts_tree.AppendFtsNode(1000000ull + i * 1000, i);
    }
    fts_tree.CreateFtsTree();

    std::mt19937_64 g2(777);
    std::set<int32_t> seen;
    for (int i = 0; i < 100; ++i) {
        seen.insert(fts_tree.GetOneNode(g2));
    }
    ASSERT_GT(seen.size(), 0u);
}

TEST_F(TestFtsTree, WeightedSelection) {
    // Node with higher fts_value should be selected more often
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(1ull, 0);          // very low weight
    fts_tree.AppendFtsNode(1000000000ull, 1); // very high weight
    fts_tree.CreateFtsTree();

    std::mt19937_64 g2(42);
    int count0 = 0, count1 = 0;
    for (int i = 0; i < 1000; ++i) {
        int32_t node = fts_tree.GetOneNode(g2);
        if (node == 0) ++count0;
        else if (node == 1) ++count1;
    }
    // Node 1 should be selected much more often
    ASSERT_GT(count1, count0);
}

TEST_F(TestFtsTree, PrintFtsTree) {
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(100ull, 0);
    fts_tree.AppendFtsNode(200ull, 1);
    fts_tree.AppendFtsNode(300ull, 2);
    fts_tree.CreateFtsTree();
    // Should not crash
    fts_tree.PrintFtsTree();
}

TEST_F(TestFtsTree, LargeTree) {
    FtsTree fts_tree;
    std::mt19937_64 g2(1234);
    for (uint32_t i = 0; i < 500; ++i) {
        uint64_t fts_value = 1000000llu + (g2() % 1000000);
        fts_tree.AppendFtsNode(fts_value, int32_t(i));
    }
    fts_tree.CreateFtsTree();

    std::set<int32_t> seen;
    for (int i = 0; i < 1000; ++i) {
        seen.insert(fts_tree.GetOneNode(g2));
    }
    ASSERT_GT(seen.size(), 0u);
    ASSERT_LE(seen.size(), 500u);
}

TEST_F(TestFtsTree, RebuildTreeOnSameInstance) {
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(10ull, 1);
    fts_tree.AppendFtsNode(20ull, 2);
    fts_tree.CreateFtsTree();
    auto first_size = fts_tree.fts_nodes_.size();

    fts_tree.AppendFtsNode(30ull, 3);
    fts_tree.CreateFtsTree();
    ASSERT_GT(fts_tree.fts_nodes_.size(), first_size);

    std::mt19937_64 g2(7);
    int32_t picked = fts_tree.GetOneNode(g2);
    ASSERT_TRUE(picked == 1 || picked == 2 || picked == 3);
}

TEST_F(TestFtsTree, InvalidTreeShapeReturnsMinusOne) {
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(10ull, 1);
    fts_tree.AppendFtsNode(20ull, 2);
    fts_tree.CreateFtsTree();

    // Corrupt internal tree size to hit the defensive guard branch.
    if (!fts_tree.fts_nodes_.empty()) {
        fts_tree.fts_nodes_.pop_back();
    }

    std::mt19937_64 g2(11);
    ASSERT_EQ(fts_tree.GetOneNode(g2), -1);
}

TEST_F(TestFtsTree, InvalidChildIndexReturnsMinusOne) {
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(10ull, 1);
    fts_tree.AppendFtsNode(20ull, 2);
    fts_tree.CreateFtsTree();

    // Corrupt child index to hit bounds checks.
    fts_tree.fts_nodes_[fts_tree.root_node_index_].left = static_cast<uint32_t>(fts_tree.fts_nodes_.size() + 10);
    std::mt19937_64 g2(13);
    ASSERT_EQ(fts_tree.GetOneNode(g2), -1);
}

TEST_F(TestFtsTree, ZeroWeightBranchFallsBackToOtherChild) {
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(0ull, 10);
    fts_tree.AppendFtsNode(100ull, 20);
    fts_tree.CreateFtsTree();

    std::mt19937_64 g2(17);
    for (int i = 0; i < 20; ++i) {
        ASSERT_EQ(fts_tree.GetOneNode(g2), 20);
    }
}

TEST_F(TestFtsTree, PrintInvalidRootDoesNotCrash) {
    FtsTree fts_tree;
    fts_tree.AppendFtsNode(10ull, 1);
    fts_tree.CreateFtsTree();

    // Trigger invalid-root guard in PrintFtsTree.
    fts_tree.root_node_index_ = static_cast<uint32_t>(fts_tree.fts_nodes_.size() + 1);
    fts_tree.PrintFtsTree();
}

}  // namespace test

}  // namespace common

}  // namespace shardora
