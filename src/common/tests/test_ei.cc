#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iostream>

#define private public
#include "common/encode.h"

namespace zjchain {

namespace common {

namespace test {

class TestEi : public testing::Test {
public:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

private:

};

TEST_F(TestEi, TestAll) {
    static const uint32_t kNodeCount = 1024;
    static const uint32_t kValidNodeCount = kNodeCount * 2 / 3 + 1;
    static const uint32_t kInvalidNodeCount = kNodeCount - kValidNodeCount;
    static const uint32_t kRoundCount = 10240;
    struct NodeInfo {
        int32_t index;
        uint32_t tx_count[kNodeCount];
        uint32_t all_tx_count;
        bool evil;
    };

    std::vector<int32_t> evil_indexs;
    std::vector<NodeInfo> nodes;
    nodes.resize(kNodeCount);
    for (uint32_t i = 0; i < kNodeCount; ++i) {
        nodes[i].index = i;
        memset(nodes[i].tx_count, 0, sizeof(nodes[i].tx_count));
        nodes[i].evil = false;
        nodes[i].all_tx_count = 0;
        evil_indexs.push_back(i);
    }

    // 随机取1/3的节点为恶意节点 
    std::random_shuffle(evil_indexs.begin(), evil_indexs.end());
    for (uint32_t i = 0 ; i < kInvalidNodeCount; ++i) {
        nodes[evil_indexs[i]].evil = true;
    }

    std::vector<int32_t> indexs = evil_indexs;
    uint32_t valid_count = 0;

    // 模拟leader共识出块，诚实leader全部选择诚实节点并累加交易数，恶意leader随机选举节点累加交易数
    for (uint32_t i = 0; i < kRoundCount; ++i) {
        uint32_t tx_count = std::rand() % 256 + 1;
        uint32_t leader_idx = i % kNodeCount;
        if (nodes[leader_idx].evil) {
            std::random_shuffle(indexs.begin(), indexs.end());
            for (int32_t idx = 0; idx < kValidNodeCount; ++idx) {
                nodes[indexs[idx]].tx_count[leader_idx] += tx_count;
                nodes[indexs[idx]].all_tx_count += tx_count;
            }
        } else {
            for (int32_t idx = 0; idx < kNodeCount; ++idx) {
                if (!nodes[idx].evil) {
                    nodes[idx].tx_count[leader_idx] += tx_count;
                    nodes[idx].all_tx_count += tx_count;
                }
            }
        }
    }

    // 按照累加值排序
    std::sort(nodes.begin(), nodes.end(), [](const NodeInfo& a, const NodeInfo& b) { 
        return a.all_tx_count > b.all_tx_count; 
    });

    // 打印排序结果，所有诚实节点都排在2/3+1之前，且和恶意节点有明显差异
    for (uint32_t i = 0; i < kNodeCount; ++i) {
        std::cout << i << "\t" << nodes[i].all_tx_count << "\t" << nodes[i].evil << std::endl;
    }
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
