#include <gtest/gtest.h>

#include <iostream>
#include <chrono>
#include <unordered_set>
#include <vector>

#define private public
#include "pools/height_tree_level.h"
#include "db/db.h"

namespace shardora {

namespace pools {

namespace test {

static std::shared_ptr<db::Db> db_ptr = nullptr;
class TestHeightTreeLevel : public testing::Test {
public:
    static void SetUpTestCase() {
        system("rm -rf ./test_height_tree_level_db");
        db_ptr = std::make_shared<db::Db>();
        db_ptr->Init("./test_height_tree_level_db");
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    void SetTreeWithInvalidHeight(uint64_t max_height, const std::set<uint64_t>& test_invalid_heidhts) {
        static int32_t i = 0;
        std::string db_path = std::string("./test_height_tree_level_db_") + std::to_string(i++);
        system((std::string("rm -rf ") + db_path).c_str());
        auto db_ptr = std::make_shared<db::Db>();
        db_ptr->Init(db_path);
        HeightTreeLevel height_tree_level(0, 0, 0, db_ptr);
        for (uint64_t i = 0; i < max_height; ++i) {
            if (test_invalid_heidhts.find(i) != test_invalid_heidhts.end()) {
                continue;
            }

            height_tree_level.Set(i);
        }

//         height_tree_level.PrintTree();
        std::vector<uint64_t> invalid_heights;
        height_tree_level.GetMissingHeights(&invalid_heights, max_height - 1);
        ASSERT_TRUE(invalid_heights.size() == test_invalid_heidhts.size());
        for (auto iter = invalid_heights.begin(); iter != invalid_heights.end(); ++iter) {
            ASSERT_TRUE(test_invalid_heidhts.find(*iter) != test_invalid_heidhts.end());
        }
    }

private:

};

TEST_F(TestHeightTreeLevel, SetValid) {
    HeightTreeLevel height_tree_level(0, 0, 0, db_ptr);
    for (uint64_t i = 0; i < 1024; ++i) {
        height_tree_level.Set(i);
    }

//     height_tree_level.PrintTree();
}

TEST_F(TestHeightTreeLevel, LoadFromDb) {
    std::vector<uint64_t> old_data;
    {
        HeightTreeLevel height_tree_level(0, 0, 0, db_ptr);
        for (uint64_t i = 0; i < kLeafMaxHeightCount * 10; ++i) {
            height_tree_level.Set(i);
        }

//         height_tree_level.PrintTree();
        db::DbWriteBatch db_batch;
        height_tree_level.FlushToDb(db_batch);
        db_ptr->Put(db_batch);
        height_tree_level.GetTreeData(&old_data);
    }

    {
        HeightTreeLevel height_tree_level(0, 0, kLeafMaxHeightCount * 10 - 1, db_ptr);
        std::vector<uint64_t> new_data;
//         height_tree_level.PrintTree();
        height_tree_level.GetTreeData(&new_data);
        ASSERT_EQ(old_data, new_data);
    }
}

TEST_F(TestHeightTreeLevel, GetInvalidHeights) {
    {
        std::set<uint64_t> test_invalid_heidhts;
        uint64_t test_max_height = 4 * kLeafMaxHeightCount;
        for (uint64_t i = 0; i < 10; ++i) {
            srand(time(NULL));
            test_invalid_heidhts.insert(rand() % test_max_height);
            test_invalid_heidhts.insert((uint64_t)pow(2, i));
        }

            SetTreeWithInvalidHeight(test_max_height, test_invalid_heidhts);
    }

    {
        std::set<uint64_t> test_invalid_heidhts;
        uint64_t test_max_height = 2 * kLeafMaxHeightCount;
        for (uint64_t i = 0; i < 10; ++i) {
            srand(time(NULL));
            test_invalid_heidhts.insert(rand() % test_max_height);
            test_invalid_heidhts.insert((uint64_t)pow(2, i));
        }

        SetTreeWithInvalidHeight(test_max_height, test_invalid_heidhts);
    }

    {
        std::set<uint64_t> test_invalid_heidhts;
        uint64_t test_max_height = 1 * kLeafMaxHeightCount;
        for (uint64_t i = 0; i < 10; ++i) {
            srand(time(NULL));
            test_invalid_heidhts.insert(rand() % test_max_height);
            test_invalid_heidhts.insert((uint64_t)pow(2, i));
        }

        for (uint64_t i = 0; i < test_invalid_heidhts.size(); ++i) {
            SetTreeWithInvalidHeight(test_max_height, test_invalid_heidhts);
        }
    }
}

}  // namespace test

}  // namespace pools

}  // namespace shardora
