#include <gtest/gtest.h>

#include <iostream>
#include <chrono>
#include <unordered_set>
#include <vector>
#include <cmath>

#define private public
#include "pools/leaf_height_tree.h"
#include "db/db.h"

namespace zjchain {

namespace pools {

namespace test {

static std::shared_ptr<db::Db> db_ptr = nullptr;
class TestLeafHeightTree : public testing::Test {
public:
    static void SetUpTestCase() {
        system("rm -rf ./test_height_tree_db");
        db_ptr = std::make_shared<db::Db>();
        db_ptr->Init("./test_height_tree_db");
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    void TestGetInvalidHeight(uint64_t height) {
        LeafHeightTree leaf_height_tree(0, 0, db_ptr);
        for (uint64_t i = 0; i < kLeafMaxHeightCount; ++i) {
            if (i == height) {
                continue;
            }

            leaf_height_tree.Set(i);
        }


        int32_t max_level = (int32_t)(log(kBranchMaxCount) / log(2));
//         std::cout << "print level max: " << max_level << std::endl;
//         for (int32_t i = max_level; i >= 0; --i) {
//             leaf_height_tree.PrintLevel(i);
//             std::cout << std::endl;
//         }
// 
        std::vector<uint64_t> get_invalid_heights;
        leaf_height_tree.GetLeafInvalidHeights(&get_invalid_heights);
        if (get_invalid_heights.empty()) {
            ASSERT_TRUE(height > leaf_height_tree.max_height_);
            return;
        }

        ASSERT_TRUE(!get_invalid_heights.empty());
        ASSERT_EQ(get_invalid_heights[0], height);
    }

private:

};

TEST_F(TestLeafHeightTree, TestGetInvalidHeights) {
    LeafHeightTree leaf_height_tree(0, 0, db_ptr);
    std::vector<uint64_t> invalid_heights;
    for (uint32_t i = 0; i < 10; ++i) {
        invalid_heights.push_back(rand() % kLeafMaxHeightCount);
    }

    for (auto iter = invalid_heights.begin(); iter != invalid_heights.end(); ++iter) {
        TestGetInvalidHeight(*iter);
    }
}

TEST_F(TestLeafHeightTree, TestSetBranch) {
    LeafHeightTree leaf_height_tree(1, 0, db_ptr);
    for (uint64_t i = 0; i < kBranchMaxCount * 2; ++i) {
        leaf_height_tree.Set(i, 0xFFFFFFFFFFFFFFFFlu);
    }

    uint64_t vec_idx = common::kInvalidUint64;
    leaf_height_tree.GetBranchInvalidNode(&vec_idx);
    ASSERT_EQ(vec_idx, common::kInvalidUint64);
    for (uint32_t i = 0; i < kBranchMaxCount * 2; ++i) {
        leaf_height_tree.Set(i, 0xFFFFFFFF1FFFFFFFlu);
        leaf_height_tree.GetBranchInvalidNode(&vec_idx);
        ASSERT_EQ(vec_idx, i / 2);

        leaf_height_tree.Set(i, 0xFFFFFFFFFFFFFFFFlu);
        vec_idx = common::kInvalidUint64;
        leaf_height_tree.GetBranchInvalidNode(&vec_idx);
        ASSERT_EQ(vec_idx, common::kInvalidUint64);
    }
}

}  // namespace test

}  // namespace pools

}  // namespace zjchain
