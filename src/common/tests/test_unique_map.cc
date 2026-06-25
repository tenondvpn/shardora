#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/unique_map.h"

namespace seth {

namespace common {

namespace test {

class TestUniqueMap : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

// UniqueMap<KeyType, ValueType, kMaxSize> — 3 template args
TEST_F(TestUniqueMap, All) {
    UniqueMap<std::string, uint64_t, 1024 * 1024> test_unique;
    for (uint64_t i = 0; i < 10000000lu; ++i) {
        ASSERT_TRUE(test_unique.add(std::to_string(i), i));
    }

    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 10), 10000000 - 10));
    ASSERT_FALSE(test_unique.add(std::to_string(10000000 - 1), 10000000 - 1));
    uint64_t val;
    ASSERT_TRUE(test_unique.get(std::to_string(10000000 - 1), &val));
    ASSERT_EQ(val, 10000000 - 1);
}

TEST_F(TestUniqueMap, Exists) {
    UniqueMap<std::string, int, 100> m;
    ASSERT_TRUE(m.add("key1", 1));
    ASSERT_TRUE(m.exists("key1"));
    ASSERT_FALSE(m.exists("key2"));
    ASSERT_FALSE(m.add("key1", 2));  // duplicate
}

TEST_F(TestUniqueMap, Erase) {
    UniqueMap<std::string, int, 100> m;
    m.add("key1", 1);
    m.add("key2", 2);
    m.erase("key1");
    ASSERT_FALSE(m.exists("key1"));
    ASSERT_TRUE(m.exists("key2"));
    m.erase("missing");  // no-op branch
    ASSERT_TRUE(m.exists("key2"));
}

TEST_F(TestUniqueMap, Eviction) {
    UniqueMap<int, int, 3> m;
    m.add(1, 10);
    m.add(2, 20);
    m.add(3, 30);
    m.add(4, 40);  // evicts 1
    ASSERT_FALSE(m.exists(1));
    ASSERT_TRUE(m.exists(4));
    int v = 0;
    ASSERT_FALSE(m.get(1, &v));  // miss branch in get()
}

TEST_F(TestUniqueMap, ZeroCapacityMapAlwaysEvicts) {
    UniqueMap<int, int, 0> m;
    ASSERT_TRUE(m.add(1, 10));
    ASSERT_EQ(m.size(), 0u);
    ASSERT_FALSE(m.exists(1));

    int v = 0;
    ASSERT_FALSE(m.get(1, &v));
}

TEST_F(TestUniqueMap, ReinsertAfterEvictionAndEraseTwice) {
    UniqueMap<int, int, 2> m;
    ASSERT_TRUE(m.add(1, 10));
    ASSERT_TRUE(m.add(2, 20));
    ASSERT_TRUE(m.add(3, 30));  // evict 1
    ASSERT_FALSE(m.exists(1));

    ASSERT_TRUE(m.add(1, 100));  // can reinsert after eviction
    int v = 0;
    ASSERT_TRUE(m.get(1, &v));
    ASSERT_EQ(v, 100);

    m.erase(1);
    m.erase(1);  // erase missing key branch after one successful erase
    ASSERT_FALSE(m.exists(1));
}

}  // namespace test

}  // namespace common

}  // namespace seth
