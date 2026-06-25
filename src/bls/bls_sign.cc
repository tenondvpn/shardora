#include "bls/bls_sign.h"

#include "common/encode.h"
#include "common/hash.h"
#include "libff/algebra/curves/alt_bn128/alt_bn128_pairing.hpp"
#include "libff/common/profiling.hpp"
#include "common/time_utils.h"

namespace shardora {

namespace bls {

namespace {

void EnsureCurveInit() {
    static bool initialized = []() {
        libff::inhibit_profiling_info = true;
        libff::inhibit_profiling_counters = true;
        libff::alt_bn128_pp::init_public_params();
        return true;
    }();
    (void)initialized;
}

const libff::alt_bn128_G2_precomp& G2OnePrecomp() {
    static const libff::alt_bn128_G2_precomp precomp =
        libff::alt_bn128_precompute_G2(libff::alt_bn128_G2::one());
    return precomp;
}

bool ValidateG1Signature(const libff::alt_bn128_G1& sign) {
    if (!sign.is_well_formed()) {
        return false;
    }

    return libff::alt_bn128_modulus_r * sign == libff::alt_bn128_G1::zero();
}

libff::alt_bn128_G1_precomp PrecomputeAffineG1(const libff::alt_bn128_G1& point) {
    libff::alt_bn128_G1 affine_point = point;
    affine_point.to_affine_coordinates();
    return libff::alt_bn128_precompute_G1(affine_point);
}

bool VerifyPairingProduct(
        const libff::alt_bn128_G1& left_g1,
        const libff::alt_bn128_G2& left_g2,
        const libff::alt_bn128_G1& right_g1,
        const libff::alt_bn128_G2& right_g2) {
    const auto prec_left_g1 = PrecomputeAffineG1(left_g1);
    const auto prec_right_g1 = PrecomputeAffineG1(right_g1);
    const auto prec_left_g2 = libff::alt_bn128_precompute_G2(left_g2);
    const auto prec_right_g2 = libff::alt_bn128_precompute_G2(right_g2);

    auto miller = libff::alt_bn128_double_miller_loop(
        prec_left_g1, prec_left_g2, prec_right_g1, prec_right_g2);
    return libff::alt_bn128_final_exponentiation(miller) == libff::alt_bn128_GT::one();
}

}  // namespace

BlsSign::BlsSign() {}

BlsSign::~BlsSign() {}

void BlsSign::Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr& secret_key,
        const libff::alt_bn128_G1& g1_hash,
        libff::alt_bn128_G1* sign) {
    (void)t;
    (void)n;
    try {
#ifdef MOCK_SIGN
        *sign = libff::alt_bn128_G1::one();
        // std::this_thread::sleep_for(std::chrono::nanoseconds(200 * 1000ull));
#else
        EnsureCurveInit();
        *sign = libBLS::Bls::Signing(g1_hash, secret_key);
        SHARDORA_DEBUG("sign message success sec: %s, hash: %s, %s, %s",
            libBLS::ThresholdUtils::fieldElementToString(secret_key).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(g1_hash.X).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(g1_hash.Y).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(g1_hash.Z).c_str());
#endif        
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

int BlsSign::VerifyFast(
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey) try {
#ifdef MOCK_VERIFY
    return kBlsSuccess;
#else
    EnsureCurveInit();
    if (!ValidateG1Signature(sign)) {
        return kBlsError;
    }

    if (!pkey.is_well_formed()) {
        return kBlsError;
    }

    // e(sign, G2) == e(hash, pk)  <=>  e(sign, G2) * e(-hash, pk) == 1
    return VerifyPairingProduct(
        sign, libff::alt_bn128_G2::one(), -g1_hash, pkey) ? kBlsSuccess : kBlsError;
#endif
} catch (std::exception& e) {
    BLS_ERROR("verify message failed: %s", e.what());
    return kBlsError;
}

bool BlsSign::AggregateVerifyFast(
        const std::vector<std::string>& str_hashes,
        const libff::alt_bn128_G1& agg_sig,
        const std::vector<libff::alt_bn128_G2>& pks) try {
    if (str_hashes.size() != pks.size()) {
        return false;
    }

    EnsureCurveInit();
    if (!ValidateG1Signature(agg_sig)) {
        return false;
    }

    // e(agg_sig, G2) == prod_i e(hash_i, pk_i)
    auto miller = libff::alt_bn128_miller_loop(
        PrecomputeAffineG1(agg_sig), G2OnePrecomp());
    for (uint32_t i = 0; i < pks.size(); ++i) {
        const auto& pk = pks[i];
        if (!pk.is_well_formed() || !libBLS::ThresholdUtils::ValidateKey(pk)) {
            throw libBLS::ThresholdUtils::IsNotWellFormed("Error, public key is invalid");
        }

        auto hash_g1 = libBLS::ThresholdUtils::HashtoG1(
            common::Encode::HexEncode(str_hashes[i]));
        miller = miller * libff::alt_bn128_miller_loop(
            PrecomputeAffineG1(-hash_g1), libff::alt_bn128_precompute_G2(pk));
    }

    return libff::alt_bn128_final_exponentiation(miller) == libff::alt_bn128_GT::one();
} catch (std::exception& e) {
    BLS_ERROR("aggregate verify failed: %s", e.what());
    return false;
}

int BlsSign::Verify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash) try {
#ifdef MOCK_VERIFY
    *verify_hash = "6276838476baeed30495988102d9261b5b8caf82b6d8f39870075f33cb14c2e6";
    return kBlsSuccess;
#else
    if (VerifyFast(sign, g1_hash, pkey) != kBlsSuccess) {
        BLS_ERROR("bls verify error.");
        return kBlsError;
    }

    libff::alt_bn128_GT res;
    if (!libBLS::Bls::GetVerifyHash(sign, &res)) {
        BLS_ERROR("bls_instance.GetVerifyHash error.");
        return kBlsError;
    }

    *verify_hash = GetVerifyHash(res);
    return kBlsSuccess;
#endif
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
#ifdef MOCK_VERIFY
    *verify_hash = "6276838476baeed30495988102d9261b5b8caf82b6d8f39870075f33cb14c2e6";
    // std::this_thread::sleep_for(std::chrono::nanoseconds(3000 * 1000ull));
    return kBlsSuccess;
#else
    EnsureCurveInit();
    libff::alt_bn128_GT res;
    if (!libBLS::Bls::GetVerifyHash(g1_hash, pkey, &res)) {
        BLS_ERROR("bls_instance.Verification error.");
        return kBlsError;
    }

    *verify_hash = GetVerifyHash(res);
    return kBlsSuccess;
#endif
} catch (std::exception& e) {
    BLS_ERROR("sign message failed: %s", e.what());
    return kBlsError;
}

int BlsSign::GetVerifyHash(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G1& sign,
        std::string* verify_hash) try {
#ifdef MOCK_VERIFY
    *verify_hash = "6276838476baeed30495988102d9261b5b8caf82b6d8f39870075f33cb14c2e6";
    // std::this_thread::sleep_for(std::chrono::nanoseconds(3000 * 1000ull));
    return kBlsSuccess;
#else
    EnsureCurveInit();
    libff::alt_bn128_GT res;
    if (!libBLS::Bls::GetVerifyHash(sign, &res)) {
        BLS_ERROR("bls_instance.Verification error.");
        return kBlsError;
    }

    *verify_hash = GetVerifyHash(res);
    return kBlsSuccess;
#endif
} catch (std::exception& e) {
    BLS_ERROR("verify message failed: %s", e.what());
    return kBlsError;
}

int BlsSign::GetLibffHash(const std::string& str_hash, libff::alt_bn128_G1* g1_hash)  try {
    EnsureCurveInit();
    *g1_hash = libBLS::Bls::Hashing(str_hash);
    return kBlsSuccess;
} catch (std::exception& e) {
    BLS_ERROR("hash message failed: %s", e.what());
    return kBlsError;
}

};  // namespace bls

};  // namespace shardora
