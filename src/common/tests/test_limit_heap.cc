#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/limit_heap.h"
#include "common/random.h"

namespace zjchain {

namespace common {

namespace test {

class TestLimitHeap : public testing::Test {
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

TEST_F(TestLimitHeap, TestMax) {
    LimitHeap<uint64_t> test_min_heap(true, 1024);
    uint64_t max_data = 0;
    for (uint64_t i = 0; i < 10000; ++i) {
        auto data = i;
        test_min_heap.push(data);
        if (max_data < data) {
            max_data = data;
        }
    }

//     ASSERT_EQ(max_data, test_min_heap.top());
    while (!test_min_heap.empty()) {
        test_min_heap.pop();
    }
}

TEST_F(TestLimitHeap, TestMin) {
    LimitHeap<uint64_t> test_min_heap(true, 1024);
    uint64_t max_data = 999999999;
    for (uint64_t i = 0; i < 10000; ++i) {
        auto data = i;
        test_min_heap.push(data);
        if (max_data > data) {
            max_data = data;
        }
    }


//     ASSERT_EQ(max_data, test_min_heap.top());
    while (!test_min_heap.empty()) {
        test_min_heap.pop();
    }
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
