#pragma once

#include <cmath>
#include <vector>
#include <random>
#include <set>

#include "common/utils.h"

namespace zjchain {

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
    int32_t GetOneNode(std::mt19937_64& g2);
    void PrintFtsTree();

private:
    std::vector<FtsNode> fts_nodes_;
    uint32_t root_node_index_{ 0 };
    uint32_t base_node_index_{ 0 };
    uint32_t valid_nodes_size_{ 0 };
};

};  // namespace common

};  // namespace zjchain
