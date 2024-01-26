#pragma once

#include <unordered_map>
#include <mutex>

#include "common/utils.h"
#include "pools/leaf_height_tree.h"

namespace zjchain {

namespace pools {

class HeightTreeLevel {
public:
    HeightTreeLevel(
        uint32_t net_id,
        uint32_t pool_index,
        uint64_t max_height,
        const std::shared_ptr<db::Db>& db);
    ~HeightTreeLevel();
    int Set(uint64_t height);
    bool Valid(uint64_t height);
    void GetMissingHeights(std::vector<uint64_t>* heights, uint64_t max_height);
    void PrintTree();
    void FlushToDb(db::DbWriteBatch& db_batch);
    void GetTreeData(std::vector<uint64_t>* data_vec);

private:
    typedef std::unordered_map<uint64_t, LeafHeightTreePtr> TreeNodeMap;
    typedef std::shared_ptr<TreeNodeMap> TreeNodeMapPtr;

    void GetHeightMaxLevel(uint64_t height, uint32_t* level, uint64_t* index);
    void BottomUpWithBrantchLevel(uint32_t level, uint64_t child_index);
    uint32_t GetMaxLevel();
    void LoadFromDb();

    static const uint32_t kMaxLevelCount = 64u;

    // Max:  2 ^ (64 - 1) * 1M * 1M block height, 
    uint32_t net_id_ = 0;
    uint32_t pool_index_ = 0;
    TreeNodeMapPtr tree_level_[kMaxLevelCount];
    uint64_t max_height_{ common::kInvalidUint64 };
    uint32_t max_level_{ 0 };
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(HeightTreeLevel);
};

};  // namespace pools

};  // namespace zjchain
