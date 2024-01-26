#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/encode.h"

namespace zjchain {

namespace common {

namespace test {

class TestEncode : public testing::Test {
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

TEST_F(TestEncode, HexEncode) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto encode_data = Encode::HexEncode(test_data);
    ASSERT_EQ("68656c6f20776f726c642ee58cbae59d97e993be0a", encode_data);
}

TEST_F(TestEncode, HexDecode) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto encode_data = Encode::HexEncode(test_data);
    ASSERT_EQ("68656c6f20776f726c642ee58cbae59d97e993be0a", encode_data);
    auto decode_data = Encode::HexDecode(encode_data);
    ASSERT_EQ(decode_data, test_data);
}

TEST_F(TestEncode, HexSubstr) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto encode_data = Encode::HexSubstr(test_data);
    ASSERT_EQ("68656c..93be0a", encode_data);
}

TEST_F(TestEncode, Base64Encode) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto encode_data = Encode::Base64Encode(test_data);
    ASSERT_EQ("aGVsbyB3b3JsZC7ljLrlnZfpk74K", encode_data);
}

TEST_F(TestEncode, Base64Decode) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto encode_data = Encode::Base64Encode(test_data);
    ASSERT_EQ("aGVsbyB3b3JsZC7ljLrlnZfpk74K", encode_data);
    auto decode_data = Encode::Base64Decode(encode_data);
    ASSERT_EQ(decode_data, test_data);
}

TEST_F(TestEncode, Base64Substr) {
    std::string test_data("helo world.区块链\n\0ASDDF");
    auto encode_data = Encode::Base64Substr(test_data);
    ASSERT_EQ("aGVsbyB..Zfpk74K", encode_data);
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
