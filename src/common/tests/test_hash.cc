#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/hash.h"
#include "common/encode.h"

namespace zjchain {

namespace common {

namespace test {

class TestHash : public testing::Test {
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

TEST_F(TestHash, Hash32) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto hash32 = Hash::Hash32(test_data);
    ASSERT_EQ(hash32, 4063693676u);
}

TEST_F(TestHash, Hash64) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto hash64 = Hash::Hash64(test_data);
    ASSERT_EQ(hash64, 9085214228960918878ull);
}

TEST_F(TestHash, Hash128) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto hash128 = Hash::Hash128(test_data);
    ASSERT_EQ(hash128.size(), 16);
    ASSERT_EQ(Encode::HexEncode(hash128), "5e3d8aa9342a157ef24ea22612939af1");
}

TEST_F(TestHash, Hash256) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto hash256 = Hash::Hash256(test_data);
    ASSERT_EQ(hash256.size(), 32);
    ASSERT_EQ(
            Encode::HexEncode(hash256),
            "5e3d8aa9342a157ef24ea22612939af17c7956ab9744235e53ae0fdcd09f5a3a");
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
