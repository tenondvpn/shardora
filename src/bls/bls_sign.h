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
        const libff::alt_bn128_G1& g1_hash,
        libff::alt_bn128_G1* common_signature);
    static int Verify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash);
    static int GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash);
    static int GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        std::string* verify_hash);
    static int GetLibffHash(const std::string& str_hash, libff::alt_bn128_G1* g1_hash);

private:
    static std::string GetVerifyHash(const libff::alt_bn128_GT& res);

    BlsSign();
    ~BlsSign();
};

};  // namespace bls

};  // namespace zjchain
