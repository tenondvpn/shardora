#include "pools/height_tree_level.h"

#include <unordered_map>
#include <cmath>
#include <cassert>

#include "common/utils.h"

namespace zjchain {

namespace pools {

HeightTreeLevel::HeightTreeLevel(
        uint64_t max_height,
        const std::shared_ptr<db::Db>& db)
        : max_height_(max_height), db_(db) {
    max_level_ = GetMaxLevel();
    LoadFromDb();
}

HeightTreeLevel::~HeightTreeLevel() {}

int HeightTreeLevel::Set(uint64_t height) {
    if (max_height_ == common::kInvalidUint64) {
        max_height_ = height;
        max_level_ = GetMaxLevel();
    }

    if (height > max_height_) {
        max_height_ = height;
        max_level_ = GetMaxLevel();
    }

    uint64_t leaf_index = height / kLeafMaxHeightCount;
    {
        TreeNodeMapPtr node_map_ptr = tree_level_[0];
        if (node_map_ptr == nullptr) {
            node_map_ptr = std::make_shared<TreeNodeMap>();
            tree_level_[0] = node_map_ptr;
        }

        LeafHeightTreePtr leaf_ptr = nullptr;
        auto iter = node_map_ptr->find(leaf_index);
        if (iter == node_map_ptr->end()) {
            leaf_ptr = std::make_shared<LeafHeightTree>(0, leaf_index, db_);
            (*node_map_ptr)[leaf_index] = leaf_ptr;
//             std::cout << "create new leaf index: " << leaf_index << std::endl;
//             if (leaf_index != 0) {
//                 (*node_map_ptr)[leaf_index - 1]->PrintTree();
//             }
// 
//             std::cout << std::endl;
        } else {
            leaf_ptr = iter->second;
        }

        leaf_ptr->Set(height);
    }

    uint64_t child_idx = height / kLeafMaxHeightCount;
    for (uint32_t level = 0; level < max_level_; ++level) {
//         std::cout << "height: " << height << ", child_idx: " << child_idx << ", max_level_: " << max_level_ << std::endl;
        BottomUpWithBrantchLevel(level, child_idx);
        child_idx = child_idx / 2 / kBranchMaxCount;
    }

    return kPoolsSuccess;
}

bool HeightTreeLevel::Valid(uint64_t height) {
    uint64_t leaf_index = height / kLeafMaxHeightCount;
    TreeNodeMapPtr node_map_ptr = tree_level_[0];
    if (node_map_ptr == nullptr) {
        return false;
    }

    LeafHeightTreePtr leaf_ptr = nullptr;
    auto iter = node_map_ptr->find(leaf_index);
    if (iter == node_map_ptr->end()) {
        return false;
    }

    return iter->second->Valid(height);
}

void HeightTreeLevel::GetMissingHeights(
        std::vector<uint64_t>* heights,
        uint64_t max_height) {
    // The higher the height, the higher the priority
    if (max_height_ == common::kInvalidUint64) {
        heights->push_back(0);
        return;
    }

    if (max_height_ != common::kInvalidUint64 && max_height > max_height_) {
        for (uint64_t i = max_height_ + 1; i <= max_height; ++i) {
            heights->push_back(i);
            if (heights->size() > 1024) {
                break;
            }
        }

        return;
    }

    auto level_map = tree_level_[max_level_];
    if (level_map == nullptr || level_map->empty()) {
        return;
    }

    auto iter = level_map->begin();
    if (max_level_ == 0) {
        iter->second->GetLeafInvalidHeights(heights);
        return;
    }

    uint64_t parent_node_idx = common::kInvalidUint64;
    iter->second->GetBranchInvalidNode(&parent_node_idx);
    if (parent_node_idx == common::kInvalidUint64) {
        return;
    }

    uint64_t parent_vec_idx = 0;
    uint32_t level_vec_index = 1;
//     std::cout << ", all max_level_: " << max_level_ << std::endl;
    for (int32_t i = (int32_t)max_level_ - 1; i >= 0; --i) {
        auto level_map = tree_level_[i];
        uint64_t left_child_vec_idx = 2 * (kBranchMaxCount * parent_vec_idx + parent_node_idx);
        uint64_t right_child_vec_idx = left_child_vec_idx + 1;
//         std::cout << "level: " << i << ", parent_vec_idx: " << parent_vec_idx << ", parent_node_idx: " << parent_node_idx
//             << ", left_child_vec_idx: " << left_child_vec_idx << ", right_child_vec_idx: " << right_child_vec_idx
//             << std::endl;
        auto liter = level_map->find(left_child_vec_idx);
        if (liter == level_map->end()) {
            return;
        }

        if (i == 0) {
            liter->second->GetLeafInvalidHeights(heights);
            if (heights->empty()) {
                auto riter = level_map->find(right_child_vec_idx);
                if (riter == level_map->end()) {
                    return;
                }

                riter->second->GetLeafInvalidHeights(heights);
            }

            return;
        }
        
        parent_node_idx = common::kInvalidUint64;
        liter->second->GetBranchInvalidNode(&parent_node_idx);
        parent_vec_idx = left_child_vec_idx;
        if (parent_node_idx == common::kInvalidUint64) {
            auto riter = level_map->find(right_child_vec_idx);
            if (riter == level_map->end()) {
                return;
            }

            riter->second->GetBranchInvalidNode(&parent_node_idx);
            parent_vec_idx = right_child_vec_idx;
        }
    }
}

void HeightTreeLevel::GetHeightMaxLevel(uint64_t height, uint32_t* level, uint64_t* index) {
    *level = 0;
    uint32_t leaf_count = height / kLeafMaxHeightCount;
    if (leaf_count == 0) {
        return;
    }

    leaf_count += 1;
    while (leaf_count > 0) {
        leaf_count /= kBranchMaxCount;
        leaf_count += 1;
        ++(*level);
    }
}

void HeightTreeLevel::BottomUpWithBrantchLevel(uint32_t level, uint64_t child_index) {
    uint32_t branch_index = child_index / 2 / kBranchMaxCount;
    ++level;
    uint64_t and_val = 0;
    uint64_t child_val1 = 0;
    uint64_t child_val2 = 0;
    {
        TreeNodeMapPtr node_map_ptr = tree_level_[level - 1];
        if (node_map_ptr == nullptr) {
            return;
        }

        LeafHeightTreePtr branch_ptr = nullptr;
        auto iter = node_map_ptr->find(child_index);
        if (iter == node_map_ptr->end()) {
            return;
        }

        child_val1 = iter->second->GetRoot();
        child_val2 = 0;
        if (child_index % 2 == 0) {
            iter = node_map_ptr->find(child_index + 1);
            if (iter != node_map_ptr->end()) {
                child_val2 = iter->second->GetRoot();
            }
        } else {
            iter = node_map_ptr->find(child_index - 1);
            if (iter != node_map_ptr->end()) {
                child_val2 = iter->second->GetRoot();
            }
        }

        and_val = child_val1 & child_val2;
    }
    
    {
        TreeNodeMapPtr node_map_ptr = tree_level_[level];
        if (node_map_ptr == nullptr) {
            node_map_ptr = std::make_shared<TreeNodeMap>();
            tree_level_[level] = node_map_ptr;
        }

        LeafHeightTreePtr branch_ptr = nullptr;
        auto iter = node_map_ptr->find(branch_index);
        if (iter == node_map_ptr->end()) {
            branch_ptr = std::make_shared<LeafHeightTree>(level, branch_index, db_);
            (*node_map_ptr)[branch_index] = branch_ptr;
//             std::cout << "create new branch level: " << level << ", index: " << branch_index << std::endl;
        } else {
            branch_ptr = iter->second;
        }

        branch_ptr->Set(child_index, and_val);
//         std::cout << "branch_index: " << branch_index << ", set branch and value child_index: " << child_index << ", child1: " << child_val1 << ", child 2: " << child_val2 << ", and_val: " << and_val << ", level: " << level << std::endl;
//         branch_ptr->PrintTree();
//         std::cout << std::endl;
    }
}

uint32_t HeightTreeLevel::GetMaxLevel() {
    if (max_height_ < kLeafMaxHeightCount || max_height_ == common::kInvalidUint64) {
        return 0;
    }

    uint32_t level = 0;
    uint64_t child_index = max_height_ / kLeafMaxHeightCount;
    while (true) {
        child_index = child_index / 2 / kBranchMaxCount;
        ++level;
        if (child_index == 0) {
            return level;
        }
    }

    return 0;
}

void HeightTreeLevel::GetTreeData(std::vector<uint64_t>* data_vec) {
    uint32_t level_vec_index = 1;
    int32_t max_level = (int32_t)(log(kBranchMaxCount) / log(2));
    for (int32_t i = (int32_t)max_level_; i >= 0; --i) {
        auto level_map = tree_level_[i];
        if (i == (int32_t)max_level_) {
            auto iter = level_map->begin();
            iter->second->GetTreeData(data_vec);
            level_vec_index = iter->second->max_vec_index() + 1;
            if (level_vec_index > kBranchMaxCount) {
                return;
            }

            continue;
        }

        level_vec_index *= 2;
        for (int32_t level_idx = max_level; level_idx >= 0; --level_idx) {
            for (uint64_t vec_idx = 0; vec_idx < level_vec_index; ++vec_idx) {
                auto iter = level_map->find(vec_idx);
                if (iter == level_map->end()) {
                    for (uint64_t zero_idx = 0; zero_idx < 2 * kBranchMaxCount - 1; ++zero_idx) {
                        data_vec->push_back(0);
                    }

                    continue;
                }

                iter->second->GetLevelData(level_idx, data_vec);
            }
        }

        level_vec_index *= kBranchMaxCount;
    }
}

void HeightTreeLevel::PrintTree() {
    std::cout << "max height: " << max_height_ << std::endl;
    uint32_t level_vec_index = 1;
    int32_t max_level = (int32_t)(log(kBranchMaxCount) / log(2));
    for (int32_t i = (int32_t)max_level_; i >= 0; --i) {
        auto level_map = tree_level_[i];
        if (i == (int32_t)max_level_) {
            auto iter = level_map->begin();
            iter->second->PrintTree();
            level_vec_index = iter->second->max_vec_index() + 1;
            if (level_vec_index > kBranchMaxCount) {
                return;
            }

            continue;
        }

        level_vec_index *= 2;
        for (int32_t level_idx = max_level; level_idx >= 0; --level_idx) {
            for (uint64_t vec_idx = 0; vec_idx < level_vec_index; ++vec_idx) {
                auto iter = level_map->find(vec_idx);
                if (iter == level_map->end()) {
                    for (uint64_t zero_idx = 0; zero_idx < 2 * kBranchMaxCount - 1; ++zero_idx) {
                        std::cout << 0 << " ";

                    }

                    continue;
                }

                assert(iter != level_map->end());
                iter->second->PrintLevel(level_idx);
            }

            std::cout << std::endl;
        }

        level_vec_index *= kBranchMaxCount;
    }
}

void HeightTreeLevel::LoadFromDb() {
    uint32_t level_vec_index = 1;
    int32_t max_level = (int32_t)max_level_;
    if (max_level == 0) {
        auto node_map_ptr = std::make_shared<TreeNodeMap>();
        tree_level_[max_level] = node_map_ptr;

        LeafHeightTreePtr branch_ptr = std::make_shared<LeafHeightTree>(0, 0, db_);
        (*node_map_ptr)[0] = branch_ptr;
        return;
    }

    for (int32_t i = (int32_t)max_level_; i >= 0; --i) {
        auto level_map = std::make_shared<TreeNodeMap>();
        tree_level_[i] = level_map;
        if (i == (int32_t)max_level_) {
            LeafHeightTreePtr branch_ptr = std::make_shared<LeafHeightTree>(i, 0, db_);
            (*level_map)[0] = branch_ptr;
            level_vec_index = branch_ptr->max_vec_index() + 1;
            continue;
        }

        level_vec_index *= 2;
        for (uint64_t vec_idx = 0; vec_idx < level_vec_index; ++vec_idx) {
            LeafHeightTreePtr branch_ptr = std::make_shared<LeafHeightTree>(i, vec_idx, db_);
            (*level_map)[vec_idx] = branch_ptr;
        }

        level_vec_index *= kBranchMaxCount;
    }
}

void HeightTreeLevel::FlushToDb() {
    uint32_t level_vec_index = 1;
    int32_t max_level = (int32_t)(log(kBranchMaxCount) / log(2));
    db::DbWriteBatch db_batch;
    for (int32_t i = (int32_t)max_level_; i >= 0; --i) {
        auto level_map = tree_level_[i];
        if (level_map == nullptr) {
            return;
        }

        if (i == (int32_t)max_level_) {
            auto iter = level_map->begin();
            iter->second->SyncToDb(db_batch);
            level_vec_index = iter->second->max_vec_index() + 1;
            if (level_vec_index > kBranchMaxCount) {
                break;
            }

            continue;
        }

        level_vec_index *= 2;
        for (uint64_t vec_idx = 0; vec_idx < level_vec_index; ++vec_idx) {
            auto iter = level_map->find(vec_idx);
            if (iter == level_map->end()) {
                break;
            }

            iter->second->SyncToDb(db_batch);
        }

        level_vec_index *= kBranchMaxCount;
    }

    if (!db_->Put(db_batch).ok()) {
        ZJC_FATAL("write db failed!");
    }
}

};  // namespace pools

};  // namespace zjchain
