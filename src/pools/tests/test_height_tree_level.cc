#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#define private public
#include "db/db.h"
#include "pools/height_tree_level.h"

namespace seth {
namespace pools {
namespace test {

class TestHeightTreeLevel : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_height_tree_level_db");
        db_ptr_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_ptr_->Init("./test_height_tree_level_db"));
    }

    static std::shared_ptr<db::Db> db_ptr_;
};

std::shared_ptr<db::Db> TestHeightTreeLevel::db_ptr_ = nullptr;

class ScopedSilenceCout {
public:
    ScopedSilenceCout() : old_(std::cout.rdbuf(oss_.rdbuf())) {}
    ~ScopedSilenceCout() {
        std::cout.rdbuf(old_);
    }

private:
    std::ostringstream oss_;
    std::streambuf* old_{ nullptr };
};

TEST_F(TestHeightTreeLevel, MissingHeightsWhenEmptyAndWhenRemoteIsAhead) {
    HeightTreeLevel level(0, 0, common::kInvalidUint64, db_ptr_);
    std::vector<uint64_t> missing;
    level.GetMissingHeights(&missing, 10);
    ASSERT_EQ(missing.size(), 1u);
    ASSERT_EQ(missing[0], 0u);

    level.Set(0);
    level.Set(1);
    missing.clear();
    level.GetMissingHeights(&missing, 5);
    ASSERT_EQ(missing, (std::vector<uint64_t>{2, 3, 4, 5}));
}

TEST_F(TestHeightTreeLevel, ValidAndGapDetectionAcrossLeaves) {
    HeightTreeLevel level(0, 1, 0, db_ptr_);
    const uint64_t max_h = kLeafMaxHeightCount * 2 + 16;
    for (uint64_t h = 0; h <= max_h; ++h) {
        if (h == 10 || h == (kLeafMaxHeightCount + 5)) {
            continue;
        }
        level.Set(h);
    }

    ASSERT_TRUE(level.Valid(0));
    ASSERT_FALSE(level.Valid(10));
    ASSERT_FALSE(level.Valid(kLeafMaxHeightCount + 5));

    std::vector<uint64_t> missing;
    level.GetMissingHeights(&missing, max_h);
    ASSERT_FALSE(missing.empty());
    ASSERT_TRUE(std::find(missing.begin(), missing.end(), 10) != missing.end());
}

TEST_F(TestHeightTreeLevel, FlushAndReloadRoundTrip) {
    std::vector<uint64_t> old_tree;
    {
        HeightTreeLevel level(2, 3, 0, db_ptr_);
        for (uint64_t h = 0; h < kLeafMaxHeightCount + 128; ++h) {
            level.Set(h);
        }
        db::DbWriteBatch batch;
        level.FlushToDb(batch);
        ASSERT_TRUE(db_ptr_->Put(batch).ok());
        level.GetTreeData(&old_tree);
    }

    HeightTreeLevel loaded(2, 3, kLeafMaxHeightCount + 127, db_ptr_);
    std::vector<uint64_t> new_tree;
    loaded.GetTreeData(&new_tree);
    ASSERT_EQ(old_tree, new_tree);
}

TEST_F(TestHeightTreeLevel, MaxLevelCalculationBranches) {
    HeightTreeLevel level(0, 4, common::kInvalidUint64, db_ptr_);
    ASSERT_EQ(level.GetMaxLevel(), 0u);

    level.max_height_ = kLeafMaxHeightCount - 1;
    ASSERT_EQ(level.GetMaxLevel(), 0u);

    level.max_height_ = kLeafMaxHeightCount * 3;
    ASSERT_GE(level.GetMaxLevel(), 1u);
}

TEST_F(TestHeightTreeLevel, InternalHelpersAndPrintPaths) {
    HeightTreeLevel level(7, 8, 0, db_ptr_);
    for (uint64_t h = 0; h < 256; ++h) {
        level.Set(h);
    }

    uint32_t h_level = 0;
    uint64_t h_index = 0;
    // Guard against infinite-loop regression in GetHeightMaxLevel when leaf_count > 0.
    level.GetHeightMaxLevel(0, &h_level, &h_index);
    ASSERT_EQ(h_level, 0u);

    std::vector<uint64_t> tree_data;
    level.GetTreeData(&tree_data);
    ASSERT_GE(tree_data.size(), 0u);
    {
        ScopedSilenceCout silence;
        level.PrintTree();
    }
}

TEST_F(TestHeightTreeLevel, MissingHeightsPathWhenTopLevelMapEmpty) {
    HeightTreeLevel level(10, 11, kLeafMaxHeightCount * 2, db_ptr_);
    level.max_level_ = 1;
    level.tree_level_[1] = std::make_shared<HeightTreeLevel::TreeNodeMap>();

    std::vector<uint64_t> missing;
    level.GetMissingHeights(&missing, kLeafMaxHeightCount * 2);
    ASSERT_TRUE(missing.empty());
}

}  // namespace test
}  // namespace pools
}  // namespace seth
