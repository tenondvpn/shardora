#include <gtest/gtest.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <tools/utils.h>
#include "bls/agg_bls.h"
#define private public

namespace shardora {

namespace bls {

namespace test {

class TestAggBls : public testing::Test {
protected:
    static void SetUpTestCase() {
        libBLS::ThresholdUtils::initCurve();
    }
    static void TearDownTestCase() {}

    void SetUp() {
        
    }
    void TearDown() {}
};

TEST_F(TestAggBls, SignAndVerify) {
    auto kp = bls::AggBls::GenerateKeyPair();
    ASSERT_TRUE(kp->IsValid());

    std::string str_hash = common::Hash::keccak256("origin message"); 
    libff::alt_bn128_G1 g1_sig = libff::alt_bn128_G1::zero();
    ASSERT_TRUE(g1_sig == libff::alt_bn128_G1::zero());
    bls::AggBls::Sign(kp->sk(), str_hash, &g1_sig);
    ASSERT_TRUE(g1_sig != libff::alt_bn128_G1::zero());

    bool ok = bls::AggBls::CoreVerify(kp->pk(), str_hash, g1_sig);
    ASSERT_TRUE(ok);
}

TEST_F(TestAggBls, PopProveAndPopVerify) {
    auto kp = bls::AggBls::GenerateKeyPair();
    ASSERT_TRUE(bls::AggBls::PopVerify(kp->pk(), kp->proof()));
}

TEST_F(TestAggBls, FastAggregateVerify) {
    std::vector<bls::AggBls::KeyPair> kps;
    std::vector<libff::alt_bn128_G2> pks;
    uint32_t n = 1024;

    for (uint32_t i = 0; i < n; i++) {
        auto kp = bls::AggBls::GenerateKeyPair();
        ASSERT_TRUE(kp->IsValid());
        kps.push_back(*kp);
        pks.push_back(kp->pk());
    }

    std::string str_hash = common::Hash::keccak256("origin message");
    std::vector<libff::alt_bn128_G1> g1_sigs;
    for (const auto& kp : kps) {
        libff::alt_bn128_G1 g1_sig = libff::alt_bn128_G1::zero();
        bls::AggBls::Sign(kp.sk_, str_hash, &g1_sig);
        ASSERT_TRUE(g1_sig != libff::alt_bn128_G1::zero());
        g1_sigs.push_back(g1_sig);
    }

    libff::alt_bn128_G1 g1_sig_agg = libff::alt_bn128_G1::zero();
    bls::AggBls::Aggregate(g1_sigs, &g1_sig_agg);

    bool ok = bls::AggBls::FastAggregateVerify(pks, str_hash, g1_sig_agg);
    ASSERT_TRUE(ok);
}

TEST_F(TestAggBls, AggregateVerify) {
    std::vector<bls::AggBls::KeyPair> kps;
    std::vector<libff::alt_bn128_G2> pks;
    uint32_t n = 5;

    for (uint32_t i = 0; i < n; i++) {
        auto kp = bls::AggBls::GenerateKeyPair();
        ASSERT_TRUE(kp->IsValid());
        kps.push_back(*kp);
    }

    ASSERT_TRUE(kps[0].pk() != kps[1].pk());
    
    std::vector<libff::alt_bn128_G1> g1_sigs;
    std::vector<std::string> str_hashes;
    for (uint32_t i = 0; i < kps.size(); i++) {
        auto kp = kps[i];
        std::string str_hash = common::Hash::keccak256("origin message" + std::to_string(i));
        libff::alt_bn128_G1 g1_sig = libff::alt_bn128_G1::zero();
        bls::AggBls::Sign(kp.sk_, str_hash, &g1_sig);
        ASSERT_TRUE(g1_sig != libff::alt_bn128_G1::zero());
        
        g1_sigs.push_back(g1_sig);
        str_hashes.push_back(str_hash);
        pks.push_back(kp.pk());
    }

    libff::alt_bn128_G1 g1_sig_agg = libff::alt_bn128_G1::zero();
    bls::AggBls::Aggregate(g1_sigs, &g1_sig_agg);
    ASSERT_TRUE(g1_sig_agg != libff::alt_bn128_G1::zero());

    bool ok = bls::AggBls::AggregateVerify(pks, str_hashes, g1_sig_agg);
    ASSERT_TRUE(ok);
}

} // namespace test

} // namespace bls

} // namespace shardora
        
