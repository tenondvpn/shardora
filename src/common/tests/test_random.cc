#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/random.h"

namespace zjchain {

namespace common {

namespace test {

class TestRandom : public testing::Test {
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

TEST_F(TestRandom, RandomInt8) {
    auto rand_val = Random::RandomInt8();
    ASSERT_NE(rand_val, 0);
}

TEST_F(TestRandom, RandomInt16) {
    auto rand_val = Random::RandomInt16();
    ASSERT_NE(rand_val, 0);
}

TEST_F(TestRandom, RandomInt32) {
    auto rand_val = Random::RandomInt32();
    ASSERT_NE(rand_val, 0);
}

TEST_F(TestRandom, RandomInt64) {
    auto rand_val = Random::RandomInt64();
    ASSERT_NE(rand_val, 0);
}

TEST_F(TestRandom, RandomUint8) {
    auto rand_val = Random::RandomUint8();
    ASSERT_NE(rand_val, 0);
}

TEST_F(TestRandom, RandomUint16) {
    auto rand_val = Random::RandomUint16();
    ASSERT_NE(rand_val, 0);
}

TEST_F(TestRandom, RandomUint32) {
    auto rand_val = Random::RandomUint32();
    ASSERT_NE(rand_val, 0);
}

TEST_F(TestRandom, RandomUint64) {
    auto rand_val = Random::RandomUint64();
    ASSERT_NE(rand_val, 0);
}

TEST_F(TestRandom, RandomString) {
    {
        auto rand_val = Random::RandomString(256);
        ASSERT_EQ(rand_val.size(), 256);
        ASSERT_NE(rand_val, "hello world");
    }
    {
        auto rand_val = Random::RandomString(4096);
        ASSERT_EQ(rand_val.size(), 4096);
        ASSERT_NE(rand_val, "hello world");
    }
    {
        auto rand_val = Random::RandomString(409600);
        ASSERT_EQ(rand_val.size(), 409600);
        ASSERT_NE(rand_val, "hello world");
    }
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
