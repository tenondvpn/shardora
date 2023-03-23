#pragma once

#include <vector>
#include <memory>

#include "common/utils.h"
#include "common/bitmap.h"
#include "db/db.h"
#include "pools/tx_utils.h"
#include "protos/prefix_db.h"

namespace zjchain {

namespace pools {

class LeafHeightTree {
public:
    LeafHeightTree(
        const std::string& db_prefix,
        uint32_t level,
        uint64_t node_index,
        const std::shared_ptr<db::Db>& db);
    LeafHeightTree(const std::vector<uint64_t>& data);
    ~LeafHeightTree();
    void Set(uint64_t bit_index);
    void Set(uint64_t child_index, uint64_t val);
    bool Valid(uint64_t bit_index);
    void GetLeafInvalidHeights(std::vector<uint64_t>* height_vec);
    void GetBranchInvalidNode(uint64_t* vec_idx);
    void SyncToDb();

    const std::vector<uint64_t>& data() const {
        return data_;
    }

    void clear() {
        for (uint32_t i = 0; i < data_.size(); ++i) {
            data_[i] = 0;
        }
    }

    uint64_t GetRoot();
    void PrintTree();
    void GetTreeData(std::vector<uint64_t>* data);
    void PrintData();
    void PrintLevel(uint32_t level);
    void GetLevelData(uint32_t level, std::vector<uint64_t>* data);

    uint32_t max_vec_index() const {
        return max_vec_index_;
    }

private:
    void ButtomUp(uint32_t vec_index);
    uint32_t GetRootIndex();
    uint32_t GetAlignMaxLevel();
    void PrintTreeFromRoot();
    void GetDataTreeFromRoot(std::vector<uint64_t>* data);
    void PrintDataFromRoot();
    void InitVec();

    void BranchButtomUp(uint32_t vec_index);
    uint32_t GetBranchAlignMaxLevel();
    uint32_t GetBranchRootIndex();
    void PrintBranchTreeFromRoot();
    void PrintBranchDataFromRoot();
    void GetDataBranchTreeFromRoot(std::vector<uint64_t>* data);
    bool LoadFromDb();

    std::vector<uint64_t> data_;
    uint64_t global_leaf_index_{ common::kInvalidUint64 };
    uint64_t max_height_{ common::kInvalidUint64 };
    std::vector<std::pair<uint32_t, uint32_t>> level_tree_index_vec_;
    bool is_branch_{ false };
    uint32_t max_vec_index_{ 0 };
    bool dirty_{ false };
    std::string db_key_;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
};

typedef std::shared_ptr<LeafHeightTree> LeafHeightTreePtr;

};  // namespace pools

};  // namespace zjchain
