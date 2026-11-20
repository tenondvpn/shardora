#pragma once

#include <cmath>
#include <vector>
#include <random>
#include <set>

#include "common/utils.h"

namespace shardora {

namespace common {

struct FtsNode {
    uint64_t fts_value;
    uint32_t parent;
    uint32_t left;
    uint32_t right;
    int32_t data{ -1 };
};

class FtsTree {
public:
    FtsTree();
    ~FtsTree();
    void AppendFtsNode(uint64_t fts_value, int32_t data);
    void CreateFtsTree();
    void PrintFtsTree();

    template<typename PRNG>
    int32_t GetOneNode(PRNG& g2) {
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

private:
    std::vector<FtsNode> fts_nodes_;
    uint32_t root_node_index_{ 0 };
    uint32_t base_node_index_{ 0 };
    uint32_t valid_nodes_size_{ 0 };
};

};  // namespace common

};  // namespace shardora
