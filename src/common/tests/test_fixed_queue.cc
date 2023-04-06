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

    while (!queue.IsEmpty()) {
        queue.Dequeue();
    }

    ASSERT_TRUE(queue.IsEmpty());
    return 0;
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
