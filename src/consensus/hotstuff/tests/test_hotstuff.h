#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <bls/bls_manager.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>

namespace shardora {

namespace hotstuff {

namespace test {

class MockBlsManager : public bls::IBlsManager {
public:
    MOCK_METHOD6(Sign, int(
                uint32_t t,
                uint32_t n,
                const libff::alt_bn128_Fr& local_sec_key,
                const libff::alt_bn128_G1& g1_hash,
                std::string* sign_x,
                std::string* sign_y));
    MOCK_METHOD5(GetVerifyHash, int(
                uint32_t t,
                uint32_t n,
                const libff::alt_bn128_G1& g1_hash,
                const libff::alt_bn128_G2& pkey,
                std::string* verify_hash));
    MOCK_METHOD4(GetVerifyHash, int(
                uint32_t t,
                uint32_t n,
                const libff::alt_bn128_G1& sign,
                std::string* verify_hash));
    MOCK_METHOD2(GetLibffHash, int(
                const std::string& str_hash,
                libff::alt_bn128_G1* g1_hash));
    
    std::shared_ptr<security::Security> security() {
        return nullptr;
    }
};

} // namespace test

} // namespace hotstuff

} // namespace shardora

