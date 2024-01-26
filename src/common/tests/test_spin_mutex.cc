#include <gtest/gtest.h>

#include <iostream>
#include <set>

#define private public
#include "common/spin_mutex.h"
#include "common/string_utils.h"
#include "common/split.h"

namespace zjchain {

namespace common {

namespace test {

class TestSpinMutex : public testing::Test {
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

TEST_F(TestSpinMutex, MultiThreadTest) {
    std::string test_str;
    common::SpinMutex spin_mutex;
    auto callback = [&test_str, &spin_mutex](int32_t thread_idx) {
        common::AutoSpinLock auto_mutex(spin_mutex);
        for (uint32_t i = 0; i < 100; ++i) {
            test_str = test_str + + " ";
        }

        common::StringUtil::Trim(test_str);
        test_str = test_str + std::to_string(thread_idx) + "_";
    };

    static const uint32_t kThreadCount = 20u;

    std::vector<std::thread*> thread_vec;
    for (uint32_t i = 0; i < kThreadCount; ++i) {
        thread_vec.push_back(new std::thread(callback, i));
    }

    for (uint32_t i = 0; i < kThreadCount; ++i) {
        thread_vec[i]->join();
    }

    std::set<uint32_t> thread_set;
    common::Split<1024> thread_split(test_str.c_str(), '_', test_str.size());
    for (uint32_t i = 0; i < thread_split.Count(); ++i) {
        uint32_t val = 0;
        if (common::StringUtil::ToUint32(thread_split[i], &val)) {
            thread_set.insert(val);
        }
    }

    ASSERT_EQ(thread_set.size(), kThreadCount);
    for (uint32_t i = 0; i < kThreadCount; ++i) {
        ASSERT_TRUE(thread_set.find(i) != thread_set.end());
    }
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
