#include <stdlib.h>
#include <math.h>

#include <iostream>

#include <gtest/gtest.h>

#include "common/utils.h"

namespace zjchain {

namespace common {

namespace test {

class TestUtils : public testing::Test {
public:
    static void SetUpTestCase() {      
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestUtils, All) {
    for (int32_t i = 0; i < 1000000; ++i) {
        std::cout << i << ":" << log2(i) << std::endl;
    }

    for (int32_t nodes_count = 3; nodes_count <= 1024; ++nodes_count) {
        int32_t expect_leader_count = (int32_t)pow(
            2.0,
            (double)((int32_t)log2(double(nodes_count / 3))));
        std::cout << nodes_count << " " << expect_leader_count << std::endl;
    }

}

}  // namespace test

}  // namespace common

}  // namespace zjchain
