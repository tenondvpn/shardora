#pragma once

#include <unordered_map>
#include <mutex>

#include "common/utils.h"
#include "sync/leaf_height_tree.h"
#include "sync/sync_utils.h"

namespace zjchain {

namespace sync {

class HeightTreeLevel {
public:
    HeightTreeLevel(
        const std::string& db_prefix,
        uint64_t max_height,
        const std::shared_ptr<db::Db>& db);
    ~HeightTreeLevel();
    int Set(uint64_t height);
    bool Valid(uint64_t height);
    void GetMissingHeights(std::vector<uint64_t>* heights, uint64_t max_height);
    void PrintTree();
    void FlushToDb();
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
    TreeNodeMapPtr tree_level_[kMaxLevelCount];
    uint64_t max_height_{ common::kInvalidUint64 };
    uint32_t max_level_{ 0 };
    std::string db_prefix_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(HeightTreeLevel);
};

};  // namespace sync

};  // namespace zjchain
