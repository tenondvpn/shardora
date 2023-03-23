#include "stdafx.h"
#include <cassert>
#include <cmath>

#include "common/global_info.h"
#include "db/db.h"
#include "protos/sync.pb.h"
#include "sync/leaf_height_tree.h"
#include "sync/sync_utils.h"

namespace zjchain {

namespace sync {

LeafHeightTree::LeafHeightTree(const std::string& db_prefix, uint32_t level, uint64_t node_index) {
    if (level == 0) {
        global_leaf_index_ = node_index * kLeafMaxHeightCount;
    } else {
        global_leaf_index_ = node_index * kBranchMaxCount;
        is_branch_ = true;
    }

    db_key_ = db_prefix + "_" + std::to_string(level) + "_" + std::to_string(node_index);
    uint32_t data_cnt = kBranchMaxCount * 2;
    for (uint32_t i = 0; i < data_cnt; ++i) {
        data_.push_back(0ull);
    }

    LoadFromDb();
    InitVec();
}


LeafHeightTree::LeafHeightTree(const std::vector<uint64_t>& data) : data_(data) {
    InitVec();
}

void LeafHeightTree::InitVec() {
    uint32_t init_level_count = kBranchMaxCount;
    uint32_t init_rate = kBranchMaxCount;
    level_tree_index_vec_.push_back(std::make_pair(0, 0));
    for (uint32_t i = 0; i < 16; ++i) {
        level_tree_index_vec_.push_back(std::make_pair(init_level_count, init_rate));
        init_rate = init_rate / 2;
        init_level_count += init_rate;
    }
}

LeafHeightTree::~LeafHeightTree() {}

void LeafHeightTree::Set(uint64_t child_index, uint64_t val) {
    uint64_t parent_idx = child_index / 2;
    if (parent_idx < global_leaf_index_) {
        assert(false);
        return;
    }

    assert(parent_idx >= global_leaf_index_ && parent_idx < global_leaf_index_ + kBranchMaxCount);
    parent_idx = parent_idx - global_leaf_index_;
    if (parent_idx >= kBranchMaxCount) {
        assert(false);
        return;
    }

    if (max_vec_index_ < parent_idx) {
        max_vec_index_ = parent_idx;
//         std::cout << "branch set max vec index: " << max_vec_index_ << std::endl;
    }

    data_[parent_idx] = val;
    BranchButtomUp(parent_idx);
    dirty_ = true;
}

void LeafHeightTree::Set(uint64_t index) {
    if (index < global_leaf_index_ || index >= global_leaf_index_ + kEachHeightTreeMaxByteSize) {
        assert(false);
        return;
    }

    if (max_height_ == common::kInvalidUint64) {
        max_height_ = index;
    }

    if (index > max_height_) {
        max_height_ = index;
    }

    index = index - global_leaf_index_;
    if (max_vec_index_ < index) {
        max_vec_index_ = index;
    }

    assert(index < kLeafMaxHeightCount);
    uint32_t vec_index = index / 64;
    uint32_t bit_index = index % 64;
    data_[vec_index] |= (uint64_t)((uint64_t)(1) << bit_index);
    ButtomUp(vec_index);
    dirty_ = true;
}

bool LeafHeightTree::Valid(uint64_t index) {
    if (index < global_leaf_index_ || index >= global_leaf_index_ + kEachHeightTreeMaxByteSize) {
        assert(false);
        return false;
    }

    index = index - global_leaf_index_;
    assert(index < (data_.size() * 64));
    uint32_t vec_index = (index % (64 * data_.size())) / 64;
    uint32_t bit_index = (index % (64 * data_.size())) % 64;
    if ((data_[vec_index] & ((uint64_t)((uint64_t)(1) << bit_index))) == 0ull) {
        return false;
    }

    return true;
}

uint32_t LeafHeightTree::GetBranchRootIndex() {
    uint32_t max_level = GetBranchAlignMaxLevel();
    return level_tree_index_vec_[max_level].first;
}

uint32_t LeafHeightTree::GetRootIndex() {
    uint32_t max_level = GetAlignMaxLevel();
    return level_tree_index_vec_[max_level].first;
}

uint32_t LeafHeightTree::GetBranchAlignMaxLevel() {
    uint32_t level = 0;
    uint32_t tmp_index = max_vec_index_;
    while (tmp_index > 0) {
        tmp_index /= 2;
        ++level;
    }

    return level;
}

uint32_t LeafHeightTree::GetAlignMaxLevel() {
    if (max_height_ == common::kInvalidUint64) {
        return 0;
    }

    uint32_t max_index = max_height_ - global_leaf_index_;
    uint32_t tmp_max_index = max_index / 64;
    if (tmp_max_index == 0) {
        return 0;
    }

    if (tmp_max_index == 1) {
        return 1;
    }

    if (tmp_max_index % 2 == 0) {
        tmp_max_index += 1;
    }

    float tmp = log(tmp_max_index) / log(2);
    if (tmp - float(int(tmp)) > (std::numeric_limits<float>::min)()) {
        tmp += 1;
    }

    return tmp;
}

void LeafHeightTree::SyncToDb() {
    if (!dirty_) {
        return;
    }

    protobuf::FlushDbItem flush_db;
    for (uint32_t i = 0; i < data_.size(); ++i) {
        flush_db.add_heights(data_[i]);
    }

    flush_db.set_max_height(max_height_);
    flush_db.set_max_vec_index(max_vec_index_);
    db::Db::Instance()->Put(db_key_, flush_db.SerializeAsString());
    dirty_ = false;
}

bool LeafHeightTree::LoadFromDb() {
    std::string data;
    auto st = db::Db::Instance()->Get(db_key_, &data);
    if (!st.ok()) {
        return false;
    }

    protobuf::FlushDbItem flush_db;
    if (!flush_db.ParseFromString(data)) {
        return false;
    }

    if (flush_db.heights_size() != kBranchMaxCount * 2) {
        return false;
    }

    max_height_ = flush_db.max_height();
    max_vec_index_ = flush_db.max_vec_index();
//     data_.clear();
    for (int32_t i = 0; i < flush_db.heights_size(); ++i) {
        data_[i] = flush_db.heights(i);
    }

    return true;
}

void LeafHeightTree::GetTreeData(std::vector<uint64_t>* data) {

}

void LeafHeightTree::PrintTree() {
    if (is_branch_) {
        PrintBranchTreeFromRoot();
    } else {
        PrintTreeFromRoot();
    }
}

uint64_t LeafHeightTree::GetRoot() {
    if (is_branch_) {
        return data_[GetBranchRootIndex()];
    }

    return data_[GetRootIndex()];
}

void LeafHeightTree::BranchButtomUp(uint32_t vec_index) {
    uint32_t max_level = GetBranchAlignMaxLevel();
    uint32_t src_vec_index = vec_index;
    for (uint32_t i = 0; i < max_level; ++i) {
        if (vec_index % 2 != 0) {
            vec_index -= 1;
        }

        uint32_t parent_index = level_tree_index_vec_[i + 1].first + src_vec_index / 2;
        data_[parent_index] = data_[vec_index] & data_[vec_index + 1];
        vec_index = parent_index;
        src_vec_index /= 2;
    }
}

void LeafHeightTree::ButtomUp(uint32_t vec_index) {
    uint32_t max_level = GetAlignMaxLevel();
    uint32_t src_vec_index = vec_index;
    for (uint32_t i = 0; i < max_level; ++i) {
        if (vec_index % 2 != 0) {
            vec_index -= 1;
        }

        uint32_t parent_index = level_tree_index_vec_[i + 1].first + src_vec_index / 2;
        data_[parent_index] = data_[vec_index] & data_[vec_index + 1];
        vec_index = parent_index;
        src_vec_index /= 2;
    }
}

void LeafHeightTree::PrintData() {
    if (is_branch_) {
        PrintBranchDataFromRoot();
    } else {
        PrintDataFromRoot();
    }
}

void LeafHeightTree::GetLevelData(uint32_t level, std::vector<uint64_t>* data) {
    uint32_t max_level = (int32_t)(log(kBranchMaxCount) / log(2));
    uint32_t level_rate = (uint32_t)pow(2.0, (max_level - level));
    uint32_t end_idx = level_tree_index_vec_[level].first + level_rate;
    for (uint32_t level_idx = level_tree_index_vec_[level].first;
            level_idx < end_idx; ++level_idx) {
        data->push_back(data_[level_idx]);
    }
}

void LeafHeightTree::PrintLevel(uint32_t level) {
    uint32_t max_level = (int32_t)(log(kBranchMaxCount) / log(2));
    uint32_t level_rate = (uint32_t)pow(2.0, (max_level - level));
    uint32_t end_idx = level_tree_index_vec_[level].first + level_rate;
    for (uint32_t level_idx = level_tree_index_vec_[level].first;
            level_idx < end_idx; ++level_idx) {
        std::cout << data_[level_idx] << " ";
    }
}

void LeafHeightTree::PrintBranchDataFromRoot() {
    int32_t max_root_index = GetBranchRootIndex();
    int32_t max_level = GetBranchAlignMaxLevel();
    std::cout << data_[max_root_index] << " ";
    uint32_t level_rate = 1;
    for (int32_t i = max_level - 1; i >= 0; --i) {
        level_rate *= 2;
        uint32_t end_idx = level_tree_index_vec_[i].first + level_rate;
        for (uint32_t level_idx = level_tree_index_vec_[i].first;
                level_idx < end_idx; ++level_idx) {
            std::cout << data_[level_idx] << " ";
        }
    }
}

void LeafHeightTree::PrintDataFromRoot() {
    int32_t max_root_index = GetRootIndex();
    int32_t max_level = GetAlignMaxLevel();
    std::cout << data_[max_root_index] << " ";
    uint32_t level_rate = 1;
    for (int32_t i = max_level - 1; i >= 0; --i) {
        level_rate *= 2;
        uint32_t end_idx = level_tree_index_vec_[i].first + level_rate;
        for (uint32_t level_idx = level_tree_index_vec_[i].first;
                level_idx < end_idx; ++level_idx) {
            std::cout << data_[level_idx] << " ";
        }
    }
}

void LeafHeightTree::GetDataBranchTreeFromRoot(std::vector<uint64_t>* data) {
    int32_t max_root_index = GetBranchRootIndex();
    data->push_back(data_[max_root_index]);
    int32_t max_level = GetBranchAlignMaxLevel();
    uint32_t level_rate = 1;
    for (int32_t i = max_level - 1; i >= 0; --i) {
        level_rate *= 2;
        uint32_t end_idx = level_tree_index_vec_[i].first + level_rate;
        for (uint32_t level_idx = level_tree_index_vec_[i].first;
                level_idx < end_idx; ++level_idx) {
            data->push_back(data_[level_idx]);
        }
    }
}

void LeafHeightTree::PrintBranchTreeFromRoot() {
    int32_t max_root_index = GetBranchRootIndex();
    std::cout << data_[max_root_index] << std::endl;
    int32_t max_level = GetBranchAlignMaxLevel();
    uint32_t level_rate = 1;
    for (int32_t i = max_level - 1; i >= 0; --i) {
        level_rate *= 2;
        uint32_t end_idx = level_tree_index_vec_[i].first + level_rate;
        for (uint32_t level_idx = level_tree_index_vec_[i].first;
                level_idx < end_idx; ++level_idx) {
            std::cout << data_[level_idx] << " ";
        }

        std::cout << std::endl;
    }
}

void LeafHeightTree::GetDataTreeFromRoot(std::vector<uint64_t>* data) {
    int32_t max_root_index = GetRootIndex();
    int32_t max_level = GetAlignMaxLevel();
    data->push_back(data_[max_root_index]);
    uint32_t level_rate = 1;
    for (int32_t i = max_level - 1; i >= 0; --i) {
        level_rate *= 2;
        uint32_t end_idx = level_tree_index_vec_[i].first + level_rate;
        for (uint32_t level_idx = level_tree_index_vec_[i].first;
                level_idx < end_idx; ++level_idx) {
            data->push_back(data_[level_idx]);
        }
    }
}

void LeafHeightTree::PrintTreeFromRoot() {
    int32_t max_root_index = GetRootIndex();
    int32_t max_level = GetAlignMaxLevel();
    std::cout << data_[max_root_index] << std::endl;
    uint32_t level_rate = 1;
    for (int32_t i = max_level - 1; i >= 0; --i) {
        level_rate *= 2;
        uint32_t end_idx = level_tree_index_vec_[i].first + level_rate;
        for (uint32_t level_idx = level_tree_index_vec_[i].first;
                level_idx < end_idx; ++level_idx) {
            std::cout << data_[level_idx] << " ";
        }

        std::cout << std::endl;
    }
}

void LeafHeightTree::GetBranchInvalidNode(uint64_t* vec_idx) {
    assert(is_branch_);
    int32_t parent_index = GetBranchRootIndex();
    if (data_[parent_index] == kLevelNodeValidHeights) {
        return;
    }

    int32_t max_level = GetBranchAlignMaxLevel();
    int32_t parent_idx = 0;
//     std::cout << "GetBranchInvalidNode: " << parent_idx << ", max_level: " << max_level << ", max_vec_idx: " << max_vec_index_ << std::endl;
    while (max_level > 0) {
        int32_t left_child_idx = level_tree_index_vec_[max_level - 1].first + parent_idx * 2;
        if (data_[left_child_idx] != kLevelNodeValidHeights) {
            parent_idx = 2 * parent_idx;
        } else {
            parent_idx = 2 * parent_idx + 1;
        }

        --max_level;
    }

    *vec_idx = level_tree_index_vec_[max_level].first + parent_idx;
}

void LeafHeightTree::GetLeafInvalidHeights(std::vector<uint64_t>* height_vec) {
    int32_t parent_index = GetRootIndex();
    if (data_[parent_index] == kLevelNodeValidHeights || max_height_ == common::kInvalidUint64) {
        return;
    }

    int32_t max_level = GetAlignMaxLevel();
    int32_t parent_level_idx = 0;
    int32_t choosed_leaf_node = 0;
    for (int32_t i = max_level - 1; i >= 0; --i) {
        int32_t left_idx = level_tree_index_vec_[i].first + parent_level_idx * 2;
        int32_t right_idx = level_tree_index_vec_[i].first + parent_level_idx * 2 + 1;
        if (data_[left_idx] != kLevelNodeValidHeights) {
            parent_level_idx = parent_level_idx * 2 ;
            choosed_leaf_node = left_idx;
        } else {
            parent_level_idx = parent_level_idx * 2 + 1;
            choosed_leaf_node = right_idx;
        }
    }

    uint64_t b_idx = global_leaf_index_ + choosed_leaf_node * 64;
//     std::cout << "GetLeafInvalidHeights GetAlignMaxLevel: " << max_level << ", b_idx: " << b_idx << ", max_height_: " << max_height_ << std::endl;
    for (uint64_t i = 0; i < 64; ++i) {
        if (b_idx + i > max_height_) {
            break;
        }

        if (!Valid(b_idx + i)) {
            height_vec->push_back(b_idx + i);
        }
    }
// 
//     if (height_vec->empty()) {
//         height_vec->push_back(max_height_ + 1);
//     }
}

}  // namespace sync

}  // namespace zjchain
