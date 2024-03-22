#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/unique_map.h"

namespace shardora {

namespace common {

namespace test {

class TestUniqueMap : public testing::Test {
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

TEST_F(TestUniqueMap, All) {
    UniqueMap<std::string, uint64_t, 1024 * 1024, 32> test_unique;
    for (uint64_t i = 0; i < 10000000lu; ++i) {
        ASSERT_TRUE(test_unique.add(std::to_string(i), i));
    }

    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 10), 10000000 - 10));
    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 1), 10000000 - 1));
    uint64_t val;
    ASSERT_TRUE(test_unique.get(std::to_string(10000000 - 1), &val));
    ASSERT_EQ(val, 10000000 - 1);
}

}  // namespace test

}  // namespace common

}  // namespace shardora
