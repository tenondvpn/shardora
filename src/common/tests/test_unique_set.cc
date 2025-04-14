#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/unique_set.h"

namespace shardora {

namespace common {

namespace test {

class TestUniqueSet : public testing::Test {
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

TEST_F(TestUniqueSet, All) {
    UniqueSet<uint64_t, 1024 * 1024, 64> test_unique;
    for (uint64_t i = 0; i < 10000000lu; ++i) {
        ASSERT_TRUE(test_unique.add(i));
    }

    ASSERT_FALSE(test_unique.add(10000000 - 10));
    ASSERT_FALSE(test_unique.add(10000000 - 1));
}

TEST_F(TestUniqueSet, String) {
    UniqueSet<std::string, 1024 * 1024, 32> test_unique;
    for (uint64_t i = 0; i < 10000000lu; ++i) {
        ASSERT_TRUE(test_unique.add(std::to_string(i)));
    }

    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 10)));
    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 1)));
}

TEST_F(TestUniqueSet, StringSet) {
    StringUniqueSet<1024 * 1024, 32> test_unique;
    for (uint64_t i = 0; i < 10000000lu; ++i) {
        ASSERT_TRUE(test_unique.add(std::to_string(i)));
    }

    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 10)));
    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 1)));
}

TEST_F(TestUniqueSet, StringPoinerSet) {
    StringUniqueSet<1024 * 1024, 32> test_unique;
    for (uint64_t i = 0; i < 10000000lu; ++i) {
        ASSERT_TRUE(test_unique.add(std::to_string(i)));
    }

    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 10)));
    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 1)));
}

}  // namespace test

}  // namespace common

}  // namespace shardora
