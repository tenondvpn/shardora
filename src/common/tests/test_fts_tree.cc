#include <gtest/gtest.h>

#include <iostream>
#include <chrono>
#include <limits>

#define private public
#include "common/fts_tree.h"
#include "common/random.h"
#include "common/encode.h"

namespace zjchain {

namespace common {

namespace test {

class TestFtsTree : public testing::Test {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

private:

};

TEST_F(TestFtsTree, all) {
    for (uint32_t i = 3; i < 4097; ++i) {
        if (i != 1000) {
            continue;
        }

        uint32_t kTestCount = i;
        FtsTree fts_tree;
        std::mt19937_64 g2(1000);
        for (uint32_t i = 0; i < kTestCount; ++i) {
            uint64_t fts_value = 1000000000llu + common::Random::RandomUint64() % 100000;
            fts_tree.AppendFtsNode(fts_value, int32_t(i));
        }

        fts_tree.CreateFtsTree();
        std::set<int32_t> node_set;
        for (uint32_t j = 0; j < i / 3; ++j) {
            node_set.insert(fts_tree.GetOneNode(g2));
        }

        ASSERT_EQ(node_set.size(), i / 3);
    }
    
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
