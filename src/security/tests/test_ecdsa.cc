#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <vector>

#include <gtest/gtest.h>
#include "openssl/aes.h"

#include "common/encode.h"
#include "common/hash.h"
#include "common/random.h"
#include "common/time_utils.h"
#define private public
#include "security/ecdsa/ecdsa.h"
#include "security/ecdsa/secp256k1.h"

namespace zjchain {

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
    std::vector<std::string> pri_vec;
    for (uint32_t i = 0; i < 10000; ++i) {
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
        ASSERT_EQ(ecdsa.Encrypt(plantext, sec1, &sectext), 0);
        std::string dectext;
        ASSERT_EQ(ecdsa.Decrypt(sectext, sec2, &dectext), 0);
        ASSERT_EQ(memcmp(plantext.c_str(), dectext.c_str(), plantext.size()), 0);
        {
            std::string msg_for_sign = common::Hash::keccak256("test");
            std::string sign;
            ASSERT_EQ(ecdsa.Sign(msg_for_sign, &sign), 0);
            ASSERT_EQ(ecdsa.Verify(msg_for_sign, pk, sign), 0);
            std::string recover_pk = ecdsa.Recover(sign, msg_for_sign);
            ASSERT_EQ(recover_pk, pk);
        }
    }
}

TEST_F(TestEcdsa, TestBench) {
    std::vector<std::string> pri_vec;
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
    ASSERT_EQ(ecdsa.Encrypt(plantext, sec1, &sectext), 0);
    std::string dectext;
    ASSERT_EQ(ecdsa.Decrypt(sectext, sec2, &dectext), 0);
    ASSERT_EQ(memcmp(plantext.c_str(), dectext.c_str(), plantext.size()), 0);
    {
        std::string msg_for_sign = common::Hash::keccak256("test");
        const uint32_t kTestCount = 100000;
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
    }
}

}  // namespace test

}  // namespace security

}  // namespace zjchain
