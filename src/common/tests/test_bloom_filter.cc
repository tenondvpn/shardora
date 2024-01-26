#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/bloom_filter.h"
#include "common/random.h"

namespace zjchain {

namespace common {

namespace test {

class TestBloomFilter : public testing::Test {
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

    for (uint32_t i = 0; i < 100; ++i) {
        auto rand_num = Random::RandomUint64();
        auto iter = std::find(check_item.begin(), check_item.end(), rand_num);
        if (iter != check_item.end()) {
            ASSERT_TRUE(bloom_filter.Contain(rand_num));
        } else {
            ASSERT_FALSE(bloom_filter.Contain(rand_num));
        }
    }

    BloomFilter copy_bloom_filter = bloom_filter;
    for (uint32_t i = 0; i < check_item.size(); ++i) {
        ASSERT_TRUE(copy_bloom_filter.Contain(check_item[i]));
    }

    for (uint32_t i = 0; i < 100; ++i) {
        auto rand_num = Random::RandomUint64();
        auto iter = std::find(check_item.begin(), check_item.end(), rand_num);
        if (iter != check_item.end()) {
            ASSERT_TRUE(copy_bloom_filter.Contain(rand_num));
        }
        else {
            ASSERT_FALSE(copy_bloom_filter.Contain(rand_num));
        }
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

    for (uint32_t i = 0; i < 100; ++i) {
        auto rand_num = Random::RandomUint64();
        auto iter = std::find(check_item.begin(), check_item.end(), rand_num);
        if (iter != check_item.end()) {
            ASSERT_TRUE(tmp_bloom_filter.Contain(rand_num));
        } else {
            ASSERT_FALSE(tmp_bloom_filter.Contain(rand_num));
        }
    }
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
