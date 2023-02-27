#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <gtest/gtest.h>

#define private public
#include "big_num/snark.h"

namespace zjchain {

namespace bignum {

namespace test {

class TestSnark : public testing::Test {
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

TEST_F(TestSnark, all) {
    
    std::cout << "test 9" << std::endl;
}

}  // namespace test

}  // namespace bignum

}  // namespace zjchain
