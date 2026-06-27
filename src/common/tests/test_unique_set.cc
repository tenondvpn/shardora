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
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

// UniqueSet<T, kMaxSize> — 2 template args
TEST_F(TestUniqueSet, All) {
    UniqueSet<uint64_t, 1024 * 1024> test_unique;
    for (uint64_t i = 0; i < 10000000lu; ++i) {
        ASSERT_TRUE(test_unique.add(i));
    }

    ASSERT_FALSE(test_unique.add(10000000 - 10));
    ASSERT_FALSE(test_unique.add(10000000 - 1));
}

TEST_F(TestUniqueSet, String) {
    UniqueSet<std::string, 1024 * 1024> test_unique;
    for (uint64_t i = 0; i < 10000000lu; ++i) {
        ASSERT_TRUE(test_unique.add(std::to_string(i)));
    }

    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 10)));
    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 1)));
}

TEST_F(TestUniqueSet, Exists) {
    UniqueSet<int, 100> s;
    ASSERT_TRUE(s.add(42));
    ASSERT_TRUE(s.exists(42));
    ASSERT_FALSE(s.exists(99));
    ASSERT_FALSE(s.add(42));  // duplicate
}

TEST_F(TestUniqueSet, Eviction) {
    UniqueSet<int, 3> s;
    s.add(1);
    s.add(2);
    s.add(3);
    s.add(4);  // evicts 1
    ASSERT_FALSE(s.exists(1));
    ASSERT_TRUE(s.exists(4));
    ASSERT_FALSE(s.add(4));  // duplicate branch after eviction
}

TEST_F(TestUniqueSet, Size) {
    UniqueSet<int, 10> s;
    ASSERT_EQ(s.size(), 0u);
    s.add(1);
    s.add(2);
    ASSERT_EQ(s.size(), 2u);
}

TEST_F(TestUniqueSet, ZeroCapacitySetAlwaysEvicts) {
    UniqueSet<int, 0> s;
    ASSERT_TRUE(s.add(7));
    ASSERT_EQ(s.size(), 0u);
    ASSERT_FALSE(s.exists(7));
}

TEST_F(TestUniqueSet, ReinsertAfterEviction) {
    UniqueSet<int, 2> s;
    ASSERT_TRUE(s.add(1));
    ASSERT_TRUE(s.add(2));
    ASSERT_TRUE(s.add(3));  // evict 1
    ASSERT_FALSE(s.exists(1));

    ASSERT_TRUE(s.add(1));   // should be insertable again
    ASSERT_TRUE(s.exists(1));
    ASSERT_FALSE(s.add(1));  // duplicate branch
}

}  // namespace test

}  // namespace common

}  // namespace shardora
