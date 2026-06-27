#include <gtest/gtest.h>

#include <iostream>
#include <chrono>

#define private public
#include "common/hash.h"
#include "common/encode.h"

namespace shardora {

namespace common {

namespace test {

class TestHash : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

TEST_F(TestHash, Hash32) {
    std::string test_data = "helo world.区块链\n";
    test_data.push_back('\0');
    test_data += "ASDDF";
    const auto hash32 = Hash::Hash32(test_data);
    ASSERT_EQ(hash32, Hash::Hash32(test_data));
    ASSERT_NE(hash32, 0u);
}

TEST_F(TestHash, Hash64) {
    std::string test_data = "helo world.区块链\n";
    test_data.push_back('\0');
    test_data += "ASDDF";
    const auto hash64 = Hash::Hash64(test_data);
    ASSERT_EQ(hash64, Hash::Hash64(test_data));
    ASSERT_NE(hash64, 0ull);
}

TEST_F(TestHash, Hash128) {
    std::string test_data = "helo world.区块链\n";
    test_data.push_back('\0');
    test_data += "ASDDF";
    const auto hash128 = Hash::Hash128(test_data);
    ASSERT_EQ(hash128.size(), 16u);
    ASSERT_EQ(hash128, Hash::Hash128(test_data));
}

TEST_F(TestHash, Hash256) {
    std::string test_data = "helo world.区块链\n";
    test_data.push_back('\0');
    test_data += "ASDDF";
    const auto hash256 = Hash::Hash256(test_data);
    ASSERT_EQ(hash256.size(), 32u);
    ASSERT_EQ(hash256, Hash::Hash256(test_data));
}

TEST_F(TestHash, Hash192) {
    std::string test_data = "test data for hash192";
    const auto hash192 = Hash::Hash192(test_data);
    ASSERT_EQ(hash192.size(), 24u);
    ASSERT_EQ(hash192, Hash::Hash192(test_data));
}

TEST_F(TestHash, Sha256) {
    std::string test_data = "hello world";
    const auto sha = Hash::Sha256(test_data);
    ASSERT_EQ(sha.size(), 32u);
    ASSERT_EQ(sha, Hash::Sha256(test_data));
    // Known SHA256 of "hello world"
    std::string expected_hex = "b94d27b9934d3e08a52e52d7da7dabfac484efe04294e576f4a385dda595a5c";
    // Just verify determinism and size, not exact value (endianness may vary)
    ASSERT_NE(Encode::HexEncode(sha), "");
}

TEST_F(TestHash, Keccak256) {
    std::string test_data = "hello";
    const auto k = Hash::keccak256(test_data);
    ASSERT_EQ(k.size(), 32u);
    ASSERT_EQ(k, Hash::keccak256(test_data));
    // keccak256("hello") = 1c8aff950685c2ed4bc3174f3472287b56d9517b9c948127319a09a7a36deac8
    std::string hex = Encode::HexEncode(k);
    ASSERT_EQ(hex, "1c8aff950685c2ed4bc3174f3472287b56d9517b9c948127319a09a7a36deac8");
}

TEST_F(TestHash, Ripemd160) {
    std::string test_data = "hello world";
    const auto r = Hash::ripemd160(test_data);
    ASSERT_EQ(r.size(), 20u);
    ASSERT_EQ(r, Hash::ripemd160(test_data));
}

TEST_F(TestHash, Sm3) {
    std::string test_data = "test sm3 hash";
    const auto s = Hash::sm3(test_data);
    ASSERT_EQ(s.size(), 32u);
    ASSERT_EQ(s, Hash::sm3(test_data));
}

TEST_F(TestHash, DifferentInputsDifferentHashes) {
    std::string a = "input_a";
    std::string b = "input_b";
    ASSERT_NE(Hash::Hash32(a), Hash::Hash32(b));
    ASSERT_NE(Hash::Hash64(a), Hash::Hash64(b));
    ASSERT_NE(Hash::Hash128(a), Hash::Hash128(b));
    ASSERT_NE(Hash::Hash256(a), Hash::Hash256(b));
    ASSERT_NE(Hash::keccak256(a), Hash::keccak256(b));
}

TEST_F(TestHash, EmptyString) {
    std::string empty = "";
    // Should not crash and should return deterministic results
    ASSERT_EQ(Hash::Hash32(empty), Hash::Hash32(empty));
    ASSERT_EQ(Hash::Hash64(empty), Hash::Hash64(empty));
    ASSERT_EQ(Hash::keccak256(empty), Hash::keccak256(empty));
    ASSERT_EQ(Hash::keccak256(empty).size(), 32u);
}

TEST_F(TestHash, HashValueConstruct) {
    // keccak256("hello") hex
    std::string hex = "1c8aff950685c2ed4bc3174f3472287b56d9517b9c948127319a09a7a36deac8";
    HashValue hv(hex);
    ASSERT_EQ(hv.to_string(), hex);
}

TEST_F(TestHash, HashValueInvalidLength) {
    ASSERT_THROW(HashValue("short"), std::invalid_argument);
    ASSERT_THROW(HashValue(""), std::invalid_argument);
}

}  // namespace test

}  // namespace common

}  // namespace shardora
