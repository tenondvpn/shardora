#pragma once

#include <libbls/tools/utils.h>
#include <libbls/bls/bls.h>
#include <libbls/bls/BLSPublicKey.h>

#include "bls/bls_utils.h"

namespace zjchain {

namespace bls {

class BlsSign {
public:
    static void Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& sec_key,
        const std::string& sign_msg,
        libff::alt_bn128_G1* common_signature);
    static int Verify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        const std::string& to_be_hashed,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash);
    static int GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const std::string& message,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash);

private:
    static std::string GetVerifyHash(const libff::alt_bn128_GT& res);

    BlsSign();
    ~BlsSign();
};

};  // namespace bls

};  // namespace zjchain
