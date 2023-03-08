#include "bls/bls_sign.h"

#include "common/encode.h"
#include "libff/common/profiling.hpp"

namespace zjchain {

namespace bls {

BlsSign::BlsSign() {}

BlsSign::~BlsSign() {}

void BlsSign::Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& secret_key,
        const libff::alt_bn128_G1& g1_hash,
        libff::alt_bn128_G1* sign) {
    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        *sign = bls_instance.Signing(g1_hash, secret_key);
    } catch (std::exception& e) {
        BLS_ERROR("sign message failed: %s", e.what());
        *sign = libff::alt_bn128_G1::zero();
    }
}

std::string BlsSign::GetVerifyHash(const libff::alt_bn128_GT& res) {
    std::string str_for_hash;
    str_for_hash.reserve(80 * 12);
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c0.c0.c0));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c0.c0.c1));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c0.c1.c0));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c0.c1.c1));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c0.c2.c0));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c0.c2.c1));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c1.c0.c0));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c1.c0.c1));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c1.c1.c0));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c1.c1.c1));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c1.c2.c0));
    str_for_hash.append(libBLS::ThresholdUtils::fieldElementToString(res.c1.c2.c1));
    return common::Hash::keccak256(str_for_hash);
}

int BlsSign::Verify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash) try {
    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    libff::alt_bn128_GT res;
    if (!bls_instance.Verification(g1_hash, sign, pkey, &res)) {
        BLS_ERROR("bls_instance.Verification error.");
        return kBlsError;
    }
    
    *verify_hash = GetVerifyHash(res);
    return kBlsSuccess;
} catch (std::exception& e) {
    BLS_ERROR("sign message failed: %s", e.what());
    return kBlsError;
}

int BlsSign::GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash) try {
    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    libff::alt_bn128_GT res;
    if (!bls_instance.GetVerifyHash(g1_hash, pkey, &res)) {
        BLS_ERROR("bls_instance.Verification error.");
        return kBlsError;
    }

    *verify_hash = GetVerifyHash(res);
    return kBlsSuccess;
} catch (std::exception& e) {
    BLS_ERROR("sign message failed: %s", e.what());
    return kBlsError;
}

int BlsSign::GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        std::string* verify_hash) try {
    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    libff::alt_bn128_GT res;
    if (!bls_instance.GetVerifyHash(sign, &res)) {
        BLS_ERROR("bls_instance.Verification error.");
        return kBlsError;
    }

    *verify_hash = GetVerifyHash(res);
    return kBlsSuccess;
} catch (std::exception& e) {
    BLS_ERROR("verify message failed: %s", e.what());
    return kBlsError;
}

int BlsSign::GetLibffHash(const std::string& str_hash, libff::alt_bn128_G1* g1_hash)  try {
    *g1_hash = libBLS::Bls::Hashing(str_hash);
    return kBlsSuccess;
} catch (std::exception& e) {
    BLS_ERROR("hash message failed: %s", e.what());
    return kBlsError;
}

};  // namespace bls

};  // namespace zjchain
