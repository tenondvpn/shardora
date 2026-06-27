#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#define private public
#include "db/db.h"
#include "pools/leaf_height_tree.h"

namespace shardora {
namespace pools {
namespace test {

class TestLeafHeightTree : public testing::Test {
public:
    static void SetUpTestSuite() {
        system("rm -rf ./test_leaf_height_tree_db");
        db_ptr_ = std::make_shared<db::Db>();
        ASSERT_TRUE(db_ptr_->Init("./test_leaf_height_tree_db"));
    }

    static std::shared_ptr<db::Db> db_ptr_;
};

std::shared_ptr<db::Db> TestLeafHeightTree::db_ptr_ = nullptr;

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

TEST_F(TestLeafHeightTree, LeafSetValidAndMissingHeightDetection) {
    LeafHeightTree tree(0, 0, 0, 0, db_ptr_);
    for (uint64_t h = 0; h <= 128; ++h) {
        if (h == 7) {
            continue;
        }
        tree.Set(h);
    }

    ASSERT_TRUE(tree.Valid(0));
    ASSERT_FALSE(tree.Valid(7));
    ASSERT_TRUE(tree.Valid(128));

    std::vector<uint64_t> missing;
    tree.GetLeafInvalidHeights(&missing);
    ASSERT_FALSE(missing.empty());
    ASSERT_EQ(missing.front(), 7u);
}

TEST_F(TestLeafHeightTree, BranchInvalidNodeSelectionAndReset) {
    LeafHeightTree branch(0, 0, 1, 0, db_ptr_);
    for (uint64_t i = 0; i < kBranchMaxCount * 2; ++i) {
        branch.Set(i, kLevelNodeValidHeights);
    }

    uint64_t invalid_vec_idx = common::kInvalidUint64;
    branch.GetBranchInvalidNode(&invalid_vec_idx);
    ASSERT_EQ(invalid_vec_idx, common::kInvalidUint64);

    branch.Set(6, 0x0);
    invalid_vec_idx = common::kInvalidUint64;
    branch.GetBranchInvalidNode(&invalid_vec_idx);
    ASSERT_NE(invalid_vec_idx, common::kInvalidUint64);

    branch.Set(6, kLevelNodeValidHeights);
    invalid_vec_idx = common::kInvalidUint64;
    branch.GetBranchInvalidNode(&invalid_vec_idx);
    ASSERT_EQ(invalid_vec_idx, common::kInvalidUint64);
}

TEST_F(TestLeafHeightTree, FlushAndReloadKeepsTreeState) {
    LeafHeightTree tree(1, 2, 0, 0, db_ptr_);
    tree.Set(1);
    tree.Set(3);
    tree.Set(65);

    db::DbWriteBatch batch;
    tree.SyncToDb(batch);
    ASSERT_TRUE(db_ptr_->Put(batch).ok());

    LeafHeightTree loaded(1, 2, 0, 0, db_ptr_);
    ASSERT_TRUE(loaded.Valid(1));
    ASSERT_TRUE(loaded.Valid(3));
    ASSERT_TRUE(loaded.Valid(65));
    ASSERT_FALSE(loaded.Valid(2));
}

TEST_F(TestLeafHeightTree, UtilityApisAndPrintPaths) {
    LeafHeightTree tree(3, 4, 0, 0, db_ptr_);
    tree.Set(0);
    tree.Set(1);
    tree.Set(64);

    std::vector<uint64_t> level_data;
    tree.GetLevelData(0, &level_data);
    ASSERT_FALSE(level_data.empty());
    ASSERT_GT(tree.GetRoot(), 0u);

    std::vector<uint64_t> scratch;
    tree.GetTreeData(&scratch);  // currently empty impl, but keep API covered.
    std::vector<uint64_t> from_root;
    tree.GetDataTreeFromRoot(&from_root);
    ASSERT_FALSE(from_root.empty());
    {
        ScopedSilenceCout silence;
        tree.PrintData();
        tree.PrintTree();
    }

    db::DbWriteBatch batch;
    LeafHeightTree clean_tree(9, 9, 0, 0, db_ptr_);
    clean_tree.SyncToDb(batch);  // cover !dirty early-return path.
}

TEST_F(TestLeafHeightTree, VectorCtorAndBranchViewDataPath) {
    std::vector<uint64_t> raw(kBranchMaxCount * 2, 0ull);
    raw[0] = 0xFFFF;
    raw[1] = 0x0FFF;
    LeafHeightTree from_vec(raw);
    ASSERT_EQ(from_vec.data().size(), kBranchMaxCount * 2);

    LeafHeightTree branch(5, 6, 1, 0, db_ptr_);
    branch.Set(0, kLevelNodeValidHeights);
    branch.Set(1, kLevelNodeValidHeights);
    std::vector<uint64_t> branch_root_data;
    branch.GetDataBranchTreeFromRoot(&branch_root_data);
    ASSERT_FALSE(branch_root_data.empty());
    {
        ScopedSilenceCout silence;
        branch.PrintData();
        branch.PrintTree();
    }
    std::vector<uint64_t> missing;
    branch.GetLeafInvalidHeights(&missing);  // branch object should no-op safely.
}

}  // namespace test
}  // namespace pools
}  // namespace shardora
