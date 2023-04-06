#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/fixed_queue.h"

namespace zjchain {

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

    while (!queue.IsEmpty()) {
        queue.Dequeue();
    }

    ASSERT_TRUE(queue.IsEmpty());
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
