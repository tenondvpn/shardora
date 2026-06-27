#include <gtest/gtest.h>
#include <cstdlib>
#include <iostream>

#include "common/encode.h"
#include "common/hash.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "security/ecdsa/ecdsa.h"
#include "security/ecdsa/secp256k1.h"

namespace shardora {

namespace security {

namespace test {

class TestEcdsa : public testing::Test {
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

TEST_F(TestEcdsa, TestAll) {
    auto test_prikey = common::Random::RandomString(32);
    Ecdsa ecdsa;
    ASSERT_EQ(ecdsa.SetPrivateKey(test_prikey), 0);
    std::string addr = ecdsa.GetAddress();
    ASSERT_EQ(addr.size(), 20u);
    std::string pk = ecdsa.GetPublicKey();
    ASSERT_EQ(pk.size(), kPublicCompressKeySize);
    ASSERT_EQ(ecdsa.GetAddress(pk), addr);
    ASSERT_TRUE(ecdsa.IsValidPublicKey(pk));
    ASSERT_FALSE(ecdsa.IsValidPublicKey(common::Encode::HexDecode(
        "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971")));

    Ecdsa ecdsa2;
    ASSERT_EQ(ecdsa2.SetPrivateKey(common::Encode::HexDecode(
        "eed7c1a51fa08b8c8b781a3b6fd1425590109be596fcaf1e77d9f57512287d2e")), 0);
    std::string pk2 = ecdsa2.GetPublicKey();
    std::string sec1;
    std::string sec2;
    ASSERT_EQ(ecdsa.GetEcdhKey(pk2, &sec1), 0);
    ASSERT_EQ(ecdsa2.GetEcdhKey(pk, &sec2), 0);
    ASSERT_EQ(sec1, sec2);

    std::string ciphertext;
    std::string plaintext = "plantext";
    RawPrivateKey raw_sec1 = std::make_pair(sec1.c_str(), static_cast<uint32_t>(sec1.size()));
    RawPrivateKey raw_sec2 = std::make_pair(sec2.c_str(), static_cast<uint32_t>(sec2.size()));
    ASSERT_EQ(ecdsa.Encrypt(plaintext, raw_sec1, &ciphertext), 0);
    std::string decrypted;
    ASSERT_EQ(ecdsa.Decrypt(ciphertext, raw_sec2, &decrypted), 0);
    ASSERT_GE(decrypted.size(), plaintext.size());
    ASSERT_EQ(decrypted.substr(0, plaintext.size()), plaintext);

    std::string msg_for_sign = common::Hash::keccak256("test");
    std::string sign;
    ASSERT_EQ(ecdsa.Sign(msg_for_sign, &sign), 0);
    ASSERT_EQ(ecdsa.Verify(msg_for_sign, pk, sign), 0);
    // Negative case: tampered message must fail verification.
    ASSERT_NE(ecdsa.Verify(common::Hash::keccak256("test2"), pk, sign), 0);
    std::string recover_pk = ecdsa.Recover(sign, msg_for_sign);
    ASSERT_EQ(recover_pk.size(), 32u);
    ASSERT_EQ(recover_pk, pk.substr(1));
}

TEST_F(TestEcdsa, TestBench) {
    if (std::getenv("SHARDORA_ENABLE_BENCH") == nullptr) {
        GTEST_SKIP() << "benchmark disabled (set SHARDORA_ENABLE_BENCH=1 to enable)";
    }

    auto test_prikey = common::Random::RandomString(32);
    Ecdsa ecdsa;
    ASSERT_EQ(ecdsa.SetPrivateKey(test_prikey), 0);
    std::string addr = ecdsa.GetAddress();
    ASSERT_TRUE(addr.size() == 20);
    std::string pk = ecdsa.GetPublicKey();
    ASSERT_EQ(pk.size(), kPublicCompressKeySize);
    std::string addr2 = ecdsa.GetAddress(pk);
    ASSERT_EQ(addr, addr2);
    ASSERT_TRUE(ecdsa.IsValidPublicKey(pk));
    ASSERT_FALSE(ecdsa.IsValidPublicKey(common::Encode::HexDecode(
        "fa04ebee157c6c10bd9d250fc2c938780bf68cbe30e9f0d7c048e4d081907971")));
    Ecdsa ecdsa2;
    ASSERT_EQ(ecdsa2.SetPrivateKey(common::Encode::HexDecode(
        "eed7c1a51fa08b8c8b781a3b6fd1425590109be596fcaf1e77d9f57512287d2e")), 0);
    std::string pk2 = ecdsa2.GetPublicKey();
    std::string sec1;
    ASSERT_EQ(ecdsa.GetEcdhKey(pk2, &sec1), 0);
    std::string sec2;
    ASSERT_EQ(ecdsa2.GetEcdhKey(pk, &sec2), 0);
    ASSERT_EQ(sec1, sec2);
    std::string enc1;
    std::string plantext = "plantext";
    std::string sectext;
    RawPrivateKey raw_sec1 = std::make_pair(sec1.c_str(), (uint32_t)sec1.size());
    RawPrivateKey raw_sec2 = std::make_pair(sec2.c_str(), (uint32_t)sec2.size());
    ASSERT_EQ(ecdsa.Encrypt(plantext, raw_sec1, &sectext), 0);
    std::string dectext;
    ASSERT_EQ(ecdsa.Decrypt(sectext, raw_sec2, &dectext), 0);
    ASSERT_EQ(memcmp(plantext.c_str(), dectext.c_str(), plantext.size()), 0);
    {
        std::string msg_for_sign = common::Hash::keccak256("test");
        const uint32_t kTestCount = 2000;
        {
            auto btime = common::TimeUtils::TimestampUs();
            for (uint32_t i = 0; i < kTestCount; ++i) {
                std::string sign;
                ASSERT_EQ(ecdsa.Sign(msg_for_sign, &sign), 0);
            }
            auto etime = common::TimeUtils::TimestampUs();
            std::cout << "sign tps: " << (double(kTestCount) / (double((etime - btime) / 1000000.0))) << std::endl;
        }
        
        {
            auto btime = common::TimeUtils::TimestampUs();
            for (uint32_t i = 0; i < kTestCount; ++i) {
                std::string sign;
                ASSERT_EQ(ecdsa.Sign(msg_for_sign, &sign), 0);
                ASSERT_EQ(ecdsa.Verify(msg_for_sign, pk, sign), 0);
            }
            auto etime = common::TimeUtils::TimestampUs();
            std::cout << "sign verify tps: " << (double(kTestCount) / (double((etime - btime) / 1000000.0))) << std::endl;
        }

        // Keep benchmark focused on the public Ecdsa interface.
    }
}

}  // namespace test

}  // namespace security

}  // namespace shardora
