#include "common/fts_tree.h"

#include <iostream>

#include "common/random.h"

namespace shardora {

namespace common {

FtsTree::FtsTree() {}

FtsTree::~FtsTree() {}

void FtsTree::AppendFtsNode(uint64_t fts_value, int32_t data) {
    const bool was_built = (root_node_index_ > 0);
    if (was_built) {
        if (fts_nodes_.size() > base_node_index_) {
            fts_nodes_.resize(base_node_index_);
        }
        root_node_index_ = 0;
    }
    fts_nodes_.push_back({ fts_value, 0, 0, 0, data });
    if (was_built || valid_nodes_size_ > 0) {
        valid_nodes_size_ = static_cast<uint32_t>(fts_nodes_.size());
        base_node_index_ = valid_nodes_size_;
    }
}

void FtsTree::CreateFtsTree() {
    if (fts_nodes_.empty()) {
        return;
    }

    if (base_node_index_ > 0 && fts_nodes_.size() > base_node_index_) {
        fts_nodes_.resize(base_node_index_);
    }

    uint32_t base_count = log2(fts_nodes_.size());
    base_node_index_ = (uint32_t)(pow(2.0, (float)base_count));
    if (base_node_index_ < fts_nodes_.size()) {
        base_count += 1;
        base_node_index_ = (uint32_t)(pow(2.0, (float)base_count));
    }

    valid_nodes_size_ = fts_nodes_.size();
    for (uint32_t i = valid_nodes_size_; i < base_node_index_; ++i) {
        auto fts_valid_idx = i % valid_nodes_size_;
        fts_nodes_.push_back({
            fts_nodes_[fts_valid_idx].fts_value,
            0,
            0,
            0,
            fts_nodes_[fts_valid_idx].data });
    }

    root_node_index_ = (uint32_t)pow(2.0f, (float)(base_count + 1)) - 2;
    for (uint32_t i = 0; ; ++i) {
        fts_nodes_[i].parent = i / 2 + (uint32_t)pow(2.0f, (float)(base_count));
        if (i % 2 != 0) {
            continue;
        }

        if (i == root_node_index_) {
            break;
        }

        auto sum_val = fts_nodes_[i].fts_value + fts_nodes_[i + 1].fts_value;
        fts_nodes_.push_back({ sum_val, 0, i, i + 1, -1 });
    }
}

void FtsTree::PrintFtsTree() {
    if (fts_nodes_.empty()) {
        std::cout << "(empty fts tree)" << std::endl;
        return;
    }

    for (uint32_t i = 0; i < fts_nodes_.size(); ++i) {
        std::cout << fts_nodes_[i].fts_value << " ";
    }

    std::cout << std::endl << std::endl;
    if (root_node_index_ >= fts_nodes_.size()) {
        std::cout << "(invalid fts tree root)" << std::endl;
        return;
    }

    int32_t level_count = 0;
    int32_t end_idx = static_cast<int32_t>(root_node_index_);
    while (true) {
        int32_t count = (int32_t)pow(2.0, (float)level_count);
        std::cout << "count: " << count << std::endl;
        for (int32_t i = end_idx - count + 1; i <= end_idx; ++i) {
            if (fts_nodes_[i].data != -1) {
                std::cout << fts_nodes_[i].fts_value << ":" << fts_nodes_[i].data << " ";
            } else {
                std::cout << fts_nodes_[i].fts_value << " ";
            }
        }

        std::cout << std::endl;
        ++level_count;
        if (end_idx - count < 0) {
            break;
        }

        end_idx = end_idx - count;
    }
}

int32_t FtsTree::GetOneNode(std::mt19937_64& g2) {
    if (fts_nodes_.empty() || fts_nodes_.size() <= root_node_index_) {
        return -1;
    }

    if (fts_nodes_.size() != root_node_index_ + 1) {
        return -1;
    }

    uint32_t choose_idx = root_node_index_;
    while (true) {
        uint64_t rand_value = 0;
        if (fts_nodes_[choose_idx].fts_value > 0) {
            auto rand_val = g2();
            SHARDORA_DEBUG("fts tree get random value: %lu", rand_val);
            rand_value = rand_val % fts_nodes_[choose_idx].fts_value;
        }

        uint32_t left_idx = fts_nodes_[choose_idx].left;
        uint32_t right_idx = fts_nodes_[choose_idx].right;
        if (left_idx >= fts_nodes_.size() || right_idx >= fts_nodes_.size()) {
            return -1;
        }

        uint64_t left_w = fts_nodes_[left_idx].fts_value;
        uint64_t right_w = fts_nodes_[right_idx].fts_value;
        if (left_w == 0 && right_w == 0) {
            return -1;
        }
        if (right_w == 0) {
            choose_idx = left_idx;
        } else if (left_w == 0) {
            choose_idx = right_idx;
        } else {
            if (fts_nodes_[left_idx].fts_value > fts_nodes_[right_idx].fts_value) {
                if (rand_value < fts_nodes_[right_idx].fts_value) {
                    choose_idx = right_idx;
                } else {
                    choose_idx = left_idx;
                }
            } else {
                if (rand_value < fts_nodes_[left_idx].fts_value) {
                    choose_idx = left_idx;
                } else {
                    choose_idx = right_idx;
                }
            }
        }

        if (choose_idx < base_node_index_) {
            return fts_nodes_[choose_idx].data;
        }
    }
    return -1;
}

};  // namespace common

};  // namespace shardora
