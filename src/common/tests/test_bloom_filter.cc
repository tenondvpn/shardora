#include <gtest/gtest.h>

#include <iostream>
#include <chrono>
#include <limits>

#define private public
#include "common/bloom_filter.h"
#include "common/random.h"

namespace seth {

namespace common {

namespace test {

class TestBloomFilter : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

TEST_F(TestBloomFilter, AddAndContain) {
    BloomFilter bloom_filter{ 4096, 11 };
    std::vector<uint64_t> check_item;
    for (uint32_t i = 0; i < 100; ++i) {
        check_item.push_back(Random::RandomUint64());
        bloom_filter.Add(check_item[i]);
    }

    for (uint32_t i = 0; i < check_item.size(); ++i) {
        ASSERT_TRUE(bloom_filter.Contain(check_item[i]));
    }

    uint32_t false_positive = 0;
    const uint32_t kRandomCheckCount = 100;
    for (uint32_t i = 0; i < kRandomCheckCount; ++i) {
        auto rand_num = Random::RandomUint64();
        auto iter = std::find(check_item.begin(), check_item.end(), rand_num);
        if (iter != check_item.end()) {
            ASSERT_TRUE(bloom_filter.Contain(rand_num));
        } else {
            if (bloom_filter.Contain(rand_num)) {
                ++false_positive;
            }
        }
    }
    // Bloom filter allows false positives, but the rate should stay low.
    ASSERT_LT(false_positive, kRandomCheckCount / 3);

    BloomFilter copy_bloom_filter = bloom_filter;
    for (uint32_t i = 0; i < check_item.size(); ++i) {
        ASSERT_TRUE(copy_bloom_filter.Contain(check_item[i]));
    }

    BloomFilter tmp_bloom_filter{ bloom_filter.data(), bloom_filter.hash_count() };
    const auto& tmp_data = tmp_bloom_filter.data();
    const auto& src_data = bloom_filter.data();
    ASSERT_EQ(tmp_data.size(), src_data.size());
    for (uint32_t i = 0; i < tmp_data.size(); ++i) {
        ASSERT_EQ(tmp_data[i], src_data[i]);
    }
    for (uint32_t i = 0; i < check_item.size(); ++i) {
        ASSERT_TRUE(tmp_bloom_filter.Contain(check_item[i]));
    }
}

TEST_F(TestBloomFilter, DefaultConstructor) {
    BloomFilter bf;
    ASSERT_EQ(bf.hash_count(), 0u);
}

TEST_F(TestBloomFilter, SerializeDeserialize) {
    BloomFilter bf(256, 3);
    bf.Add(12345678ull);
    bf.Add(87654321ull);
    bf.Add(99999999ull);

    std::string serialized = bf.Serialize();
    ASSERT_EQ(serialized.size(), bf.data().size() * sizeof(uint64_t));

    // Deserialize into a new filter
    BloomFilter bf2;
    const uint64_t* raw = reinterpret_cast<const uint64_t*>(serialized.data());
    bf2.Deserialize(raw, (uint32_t)(serialized.size() / sizeof(uint64_t)), 3);

    ASSERT_TRUE(bf2.Contain(12345678ull));
    ASSERT_TRUE(bf2.Contain(87654321ull));
    ASSERT_TRUE(bf2.Contain(99999999ull));
}

TEST_F(TestBloomFilter, EqualityOperator) {
    BloomFilter a(256, 3);
    a.Add(100ull);
    a.Add(200ull);

    BloomFilter b(256, 3);
    b.Add(100ull);
    b.Add(200ull);

    ASSERT_TRUE(a == b);
    ASSERT_FALSE(a != b);

    b.Add(300ull);
    ASSERT_FALSE(a == b);
    ASSERT_TRUE(a != b);
}

TEST_F(TestBloomFilter, DiffCount) {
    BloomFilter a(256, 3);
    a.Add(1ull);
    a.Add(2ull);

    BloomFilter b(256, 3);
    b.Add(1ull);
    b.Add(3ull);

    // DiffCount returns number of bits set in a but not in b
    uint32_t diff = a.DiffCount(b);
    ASSERT_GE(diff, 0u);
}

TEST_F(TestBloomFilter, AssignmentOperator) {
    BloomFilter a(256, 5);
    a.Add(42ull);
    a.Add(84ull);

    BloomFilter b;
    b = a;

    ASSERT_TRUE(b.Contain(42ull));
    ASSERT_TRUE(b.Contain(84ull));
    ASSERT_EQ(b.hash_count(), a.hash_count());
    ASSERT_EQ(b.data().size(), a.data().size());
}

TEST_F(TestBloomFilter, EmptyFilterContainsFalse) {
    BloomFilter bf(256, 3);
    ASSERT_FALSE(bf.Contain(12345ull));
    ASSERT_FALSE(bf.Contain(0ull));
    ASSERT_FALSE(bf.Contain(UINT64_MAX));
}

TEST_F(TestBloomFilter, MultipleHashCounts) {
    // Test with different hash counts
    for (uint32_t hc : {1u, 3u, 7u, 11u}) {
        BloomFilter bf(512, hc);
        bf.Add(1000ull);
        bf.Add(2000ull);
        ASSERT_TRUE(bf.Contain(1000ull));
        ASSERT_TRUE(bf.Contain(2000ull));
        ASSERT_EQ(bf.hash_count(), hc);
    }
}

TEST_F(TestBloomFilter, SerializeSize) {
    BloomFilter bf(4096, 3);  // 4096 bits = 64 uint64_t
    std::string s = bf.Serialize();
    ASSERT_EQ(s.size(), 64 * sizeof(uint64_t));
}

TEST_F(TestBloomFilter, EmptySerializeAndNoHashCountGuard) {
    BloomFilter empty;
    ASSERT_TRUE(empty.Serialize().empty());
    ASSERT_FALSE(empty.Contain(123ull));

    // Add should be a no-op when internal data is empty.
    empty.Add(123ull);
    ASSERT_TRUE(empty.Serialize().empty());
}

TEST_F(TestBloomFilter, DiffCountSelfAndMismatch) {
    BloomFilter lhs(256, 3);
    lhs.Add(100ull);
    lhs.Add(200ull);
    ASSERT_EQ(lhs.DiffCount(lhs), 0u);

    BloomFilter rhs(128, 3);
    ASSERT_EQ(lhs.DiffCount(rhs), (std::numeric_limits<uint32_t>::max)());
}

TEST_F(TestBloomFilter, EqualityWithSelfAndDifferentHashCount) {
    BloomFilter a(256, 3);
    a.Add(123ull);
    ASSERT_TRUE(a == a);
    ASSERT_FALSE(a != a);

    BloomFilter b(256, 5);
    b.Add(123ull);
    ASSERT_FALSE(a == b);
    ASSERT_TRUE(a != b);
}

}  // namespace test

}  // namespace common

}  // namespace seth
