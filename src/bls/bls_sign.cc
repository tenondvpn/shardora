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
        const std::string& message,
        libff::alt_bn128_G1* sign) {
    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        libff::alt_bn128_G1 hash = bls_instance.Hashing(message);
        *sign = bls_instance.Signing(hash, secret_key);
    } catch (std::exception& e) {
        BLS_ERROR("sign message failed: %s", e.what());
        *sign = libff::alt_bn128_G1::zero();
    }
}

int BlsSign::Verify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        const std::string& message,
        const libff::alt_bn128_G2& pkey) try {
    auto sign_ptr = const_cast<libff::alt_bn128_G1*>(&sign);
    sign_ptr->to_affine_coordinates();
    auto sign_x = libBLS::ThresholdUtils::fieldElementToString(sign_ptr->X);
    auto sign_y = libBLS::ThresholdUtils::fieldElementToString(sign_ptr->Y);
    auto pk = const_cast<libff::alt_bn128_G2*>(&pkey);
    pk->to_affine_coordinates();
    auto pk_ptr = std::make_shared<BLSPublicKey>(*pk);
    auto strs = pk_ptr->toString();
    BLS_DEBUG("verify t: %u, n: %u, public key: %s,%s,%s,%s, "
        "msg hash: %s, sign x: %s, sign y: %s",
        t, n, strs->at(0).c_str(), strs->at(1).c_str(),
        strs->at(2).c_str(), strs->at(3).c_str(),
        common::Encode::HexEncode(message).c_str(),
        sign_x.c_str(),
        sign_y.c_str());

    if (!sign.is_well_formed()) {
        BLS_ERROR("sign.is_well_formed() error.");
        return kBlsError;
    }

    if (!pkey.is_well_formed()) {
        BLS_ERROR("pkey.is_well_formed() error.");
        return kBlsError;
    }

    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    if (!bls_instance.Verification(message, sign, pkey)) {
        BLS_ERROR("bls_instance.Verification error.");
        return kBlsError;
    }
    
    return kBlsSuccess;
} catch (std::exception& e) {
    BLS_ERROR("sign message failed: %s", e.what());
    return kBlsError;
}

};  // namespace bls

};  // namespace zjchain
