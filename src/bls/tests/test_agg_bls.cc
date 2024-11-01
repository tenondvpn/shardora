#include <gtest/gtest.h>
#include "bls/agg_bls.h"
#define private public

namespace shardora {

namespace bls {

namespace test {

class TestAggBls : public testing::Test {
protected:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}

    void SetUp() {}
    void TearDown() {}
};

TEST_F(TestAggBls, SignAndVerify) {
    auto kp = bls::AggBls::GenerateKeyPair();

    std::string str_hash = common::Hash::keccak256("origin message"); 
    libff::alt_bn128_G1 g1_sig;
    bls::AggBls::Sign(kp->sk(), str_hash, &g1_sig);

    bool ok = bls::AggBls::CoreVerify(kp->pk(), str_hash, g1_sig);
    ASSERT_TRUE(ok);
}

TEST_F(TestAggBls, PopProveAndPopVerify) {
    auto kp = bls::AggBls::GenerateKeyPair();
    ASSERT_TRUE(bls::AggBls::PopVerify(kp->pk(), kp->proof()));
}

} // namespace test

} // namespace bls

} // namespace shardora
        
