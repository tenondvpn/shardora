#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/fixed_queue.h"

namespace shardora {

namespace common {

namespace test {

class TestFixedQueue : public testing::Test {
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

TEST_F(TestFixedQueue, all) {
    FixedQueue<std::string, 10> queue;
    for (int i = 0; i < 10; i++) {
        queue.Enqueue(std::to_string(i));
    }

    ASSERT_EQ(queue.Front(), "0");
    ASSERT_EQ(queue.Rear(), "9");
    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(queue.Exists(std::to_string(i)));
    }

    for (int i = 10; i < 110; i++) {
        ASSERT_FALSE(queue.Exists(std::to_string(i)));
    }

    int32_t t = 0;
    while (!queue.IsEmpty()) {
        queue.Dequeue();
        ++t;
        for (int i = 0; i < t; ++i) {
            ASSERT_FALSE(queue.Exists(std::to_string(i)));
        }

        for (int i = t; i < 10; i++) {
            ASSERT_TRUE(queue.Exists(std::to_string(i)));
        }
    }

    ASSERT_TRUE(queue.IsEmpty());
}

TEST_F(TestFixedQueue, EmptyFrontRearAndDequeueNoop) {
    FixedQueue<int, 4> queue;
    ASSERT_TRUE(queue.IsEmpty());
    ASSERT_EQ(queue.Size(), 0);
    ASSERT_EQ(queue.Front(), 0);
    ASSERT_EQ(queue.Rear(), 0);
    ASSERT_FALSE(queue.Exists(1));

    queue.Dequeue();  // empty dequeue branch
    ASSERT_TRUE(queue.IsEmpty());
    ASSERT_EQ(queue.Size(), 0);
}

TEST_F(TestFixedQueue, EnqueueWhenFullIsNoopAndWraparoundExistsBranch) {
    FixedQueue<int, 4> queue;
    queue.Enqueue(1);
    queue.Enqueue(2);
    queue.Enqueue(3);
    queue.Enqueue(4);
    ASSERT_TRUE(queue.IsFull());
    ASSERT_EQ(queue.Rear(), 4);

    queue.Enqueue(5);  // full queue branch: ignored
    ASSERT_EQ(queue.Size(), 4);
    ASSERT_EQ(queue.Rear(), 4);
    ASSERT_FALSE(queue.Exists(5));

    // Create rear_ < front_ wraparound region to hit Exists() final branch.
    queue.Dequeue();  // remove 1
    queue.Dequeue();  // remove 2
    queue.Enqueue(6);
    queue.Enqueue(7);
    ASSERT_TRUE(queue.Exists(6));
    ASSERT_TRUE(queue.Exists(7));
    ASSERT_TRUE(queue.Exists(3));
    ASSERT_FALSE(queue.Exists(1));
    ASSERT_FALSE(queue.Exists(2));
}

}  // namespace test

}  // namespace common

}  // namespace shardora
