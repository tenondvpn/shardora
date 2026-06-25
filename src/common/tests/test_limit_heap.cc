#include <gtest/gtest.h>

#include <iostream>
#include <chrono>
#include <algorithm>

#define private public
#include "common/limit_heap.h"
#include "common/random.h"

namespace seth {

namespace common {

namespace test {

class TestLimitHeap : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
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

    while (!test_min_heap.empty()) {
        test_min_heap.pop();
    }
}

TEST_F(TestLimitHeap, EmptyHeap) {
    LimitHeap<uint64_t> heap(false, 10);
    ASSERT_TRUE(heap.empty());
    ASSERT_EQ(heap.size(), 0);
}

TEST_F(TestLimitHeap, SingleElement) {
    LimitHeap<uint64_t> heap(false, 10);
    heap.push(42ull);
    ASSERT_FALSE(heap.empty());
    ASSERT_EQ(heap.size(), 1);
    ASSERT_EQ(heap.top(), 42ull);
    heap.pop();
    ASSERT_TRUE(heap.empty());
}

TEST_F(TestLimitHeap, MinHeapProperty) {
    LimitHeap<uint64_t> heap(false, 100);
    // Push in reverse order
    for (uint64_t i = 100; i > 0; --i) {
        heap.push(i);
    }
    ASSERT_EQ(heap.size(), 100);
    ASSERT_EQ(heap.top(), 1ull);  // Min should be at top
}

TEST_F(TestLimitHeap, MaxSizeLimit) {
    LimitHeap<uint64_t> heap(false, 5);
    for (uint64_t i = 1; i <= 10; ++i) {
        heap.push(i);
    }
    // Should not exceed max_size
    ASSERT_LE((uint32_t)heap.size(), 5u);
}

TEST_F(TestLimitHeap, PushRejectedWhenFullAndValueWorseThanTop) {
    LimitHeap<uint64_t> heap(false, 3);
    heap.push(1);
    heap.push(2);
    heap.push(3);
    ASSERT_EQ(heap.size(), 3u);
    // Min-heap top is 1; pushing 0 hits early reject branch in current logic.
    ASSERT_EQ(heap.push(0), -1);
    ASSERT_EQ(heap.size(), 3u);
}

TEST_F(TestLimitHeap, UniqueConstraint) {
    LimitHeap<uint64_t> heap(true, 100);
    heap.push(42ull);
    heap.push(42ull);  // Duplicate should be rejected
    heap.push(42ull);  // Duplicate should be rejected
    ASSERT_EQ(heap.size(), 1);
}

TEST_F(TestLimitHeap, UniqueConstraintMultipleValues) {
    LimitHeap<uint64_t> heap(true, 100);
    for (uint64_t i = 0; i < 10; ++i) {
        heap.push(i);
    }
    // Try to push duplicates
    for (uint64_t i = 0; i < 10; ++i) {
        heap.push(i);
    }
    ASSERT_EQ(heap.size(), 10);
}

TEST_F(TestLimitHeap, NonUniqueAllowsDuplicates) {
    LimitHeap<uint64_t> heap(false, 100);
    heap.push(5ull);
    heap.push(5ull);
    heap.push(5ull);
    ASSERT_EQ(heap.size(), 3);
}

TEST_F(TestLimitHeap, CopyConstructor) {
    LimitHeap<uint64_t> original(false, 10);
    original.push(3ull);
    original.push(1ull);
    original.push(2ull);

    LimitHeap<uint64_t> copy(original);
    ASSERT_EQ(copy.size(), original.size());
    ASSERT_EQ(copy.top(), original.top());
}

TEST_F(TestLimitHeap, AssignmentOperator) {
    LimitHeap<uint64_t> a(false, 10);
    a.push(10ull);
    a.push(5ull);

    LimitHeap<uint64_t> b(false, 10);
    b = a;
    ASSERT_EQ(b.size(), a.size());
    ASSERT_EQ(b.top(), a.top());
}

TEST_F(TestLimitHeap, AdjustUpDown) {
    LimitHeap<uint64_t> heap(false, 20);
    // Push values that require both AdjustUp and AdjustDown
    std::vector<uint64_t> vals = {5, 3, 8, 1, 9, 2, 7, 4, 6};
    for (auto v : vals) {
        heap.push(v);
    }
    ASSERT_EQ(heap.top(), 1ull);

    heap.pop();
    ASSERT_EQ(heap.top(), 2ull);

    heap.pop();
    ASSERT_EQ(heap.top(), 3ull);
}

TEST_F(TestLimitHeap, StringType) {
    LimitHeap<std::string> heap(true, 10);
    heap.push("banana");
    heap.push("apple");
    heap.push("cherry");
    heap.push("apple");  // duplicate
    ASSERT_EQ(heap.size(), 3);
}

TEST_F(TestLimitHeap, Int32Type) {
    LimitHeap<int32_t> heap(false, 10);
    heap.push(5);
    heap.push(-3);
    heap.push(0);
    heap.push(10);
    ASSERT_EQ(heap.top(), -3);
}

TEST_F(TestLimitHeap, ParentIndexCalculation) {
    LimitHeap<uint64_t> heap(false, 100);
    // Even index: parent = index/2 - 1
    ASSERT_EQ(heap.ParentIndex(2), 0);
    ASSERT_EQ(heap.ParentIndex(4), 1);
    // Odd index: parent = index/2
    ASSERT_EQ(heap.ParentIndex(1), 0);
    ASSERT_EQ(heap.ParentIndex(3), 1);
}

TEST_F(TestLimitHeap, AssignmentSelfAndPopOnNonUnique) {
    LimitHeap<uint64_t> heap(false, 10);
    heap.push(3);
    heap.push(1);
    heap.push(2);
    heap = heap;  // self-assignment branch
    ASSERT_EQ(heap.top(), 1u);
    heap.pop();
    ASSERT_EQ(heap.top(), 2u);
}

TEST_F(TestLimitHeap, ChildIndexCalculation) {
    LimitHeap<uint64_t> heap(false, 100);
    ASSERT_EQ(heap.LeftChild(0), 1);
    ASSERT_EQ(heap.RightChild(0), 2);
    ASSERT_EQ(heap.LeftChild(1), 3);
    ASSERT_EQ(heap.RightChild(1), 4);
}

TEST_F(TestLimitHeap, UniquePopRemovesFromUniqueSetAllowingReinsert) {
    LimitHeap<uint64_t> heap(true, 10);
    ASSERT_EQ(heap.push(9), 0);
    ASSERT_EQ(heap.push(1), 0);
    ASSERT_EQ(heap.push(5), 2);
    ASSERT_EQ(heap.size(), 3u);

    // Pop removes top from unique_set_, so value 1 can be inserted again.
    heap.pop();
    ASSERT_EQ(heap.push(1), 0);
    ASSERT_EQ(heap.size(), 3u);
}

TEST_F(TestLimitHeap, AdjustDownSingleLeftChildSwapPath) {
    LimitHeap<uint64_t> heap(false, 8);
    // Build shape where index 0 has only left child and needs swap.
    heap.size_ = 2;
    heap.data_[0] = 10;
    heap.data_[1] = 3;
    ASSERT_EQ(heap.AdjustDown(0), 1);
    ASSERT_EQ(heap.top(), 3u);
}

TEST_F(TestLimitHeap, AdjustDownChoosesRightChildBranch) {
    LimitHeap<uint64_t> heap(false, 8);
    // left=8 right=2, both better than parent=10, should swap with right child.
    heap.size_ = 3;
    heap.data_[0] = 10;
    heap.data_[1] = 8;
    heap.data_[2] = 2;
    ASSERT_EQ(heap.AdjustDown(0), 2);
    ASSERT_EQ(heap.top(), 2u);
}

}  // namespace test

}  // namespace common

}  // namespace seth
