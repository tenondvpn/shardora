#include "big_num/snark.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "libff/algebra/curves/alt_bn128/alt_bn128_pp.hpp"
#include "libff/common/profiling.hpp"

namespace shardora {

namespace bignum {

Snark* Snark::Instance() {
    static Snark ins;
    return &ins;
}

std::string Snark::AltBn128PairingProduct(const std::string& in) {
    size_t constexpr pair_size = 2 * 32 + 2 * 64;
    size_t const pairs = in.size() / pair_size;
    if (pairs * pair_size != in.size()) {
        return "";
    }

    try {
        InitLibSnark();
        libff::alt_bn128_Fq12 x = libff::alt_bn128_Fq12::one();
        for (size_t i = 0; i < pairs; ++i) {
            std::string pair = in.substr(i * pair_size, pair_size);
            libff::alt_bn128_G1 const g1 = DecodePointG1(pair);
            libff::alt_bn128_G2 const p = DecodePointG2(pair.substr(2 * 32, pair.size() - 2 * 32));
            if (-libff::alt_bn128_G2::scalar_field::one() * p + p != libff::alt_bn128_G2::zero()) {
                return "";
            }

            if (p.is_zero() || g1.is_zero()) {
                continue;
            }

            x = x * libff::alt_bn128_miller_loop(
                libff::alt_bn128_precompute_G1(g1),
                libff::alt_bn128_precompute_G2(p));
        }

        bool const result = libff::alt_bn128_final_exponentiation(x) == libff::alt_bn128_GT::one();
        char data[32];
        memset(data, 0, sizeof(data));
        if (result) {
            data[31] = 1;
        }

        return std::string(data, sizeof(data));
    } catch (...) {
        return "";
    }
}

std::string Snark::AltBn128G1Add(const std::string& in) {
    try {
        if (in.size() != 128) {
            return "";
        }
        InitLibSnark();
        libff::alt_bn128_G1 const p1 = DecodePointG1(in.substr(0, 64));
        libff::alt_bn128_G1 const p2 = DecodePointG1(in.substr(64, 64));
        return EncodePointG1(p1 + p2);
    } catch (...) {
        return "";
    }
}

std::string Snark::AltBn128G1Mul(const std::string& in) {
    try {
        if (in.size() != 96) {
            return "";
        }
        InitLibSnark();
        libff::alt_bn128_G1 const p = DecodePointG1(in.substr(0, 64));
        libff::alt_bn128_G1 const result = ToLibsnarkBigint(in.substr(64, 32)) * p;
        return EncodePointG1(result);
    } catch (...) {
        return "";
    }
}

void Snark::InitLibSnark() {
    static bool s_initialized = []() noexcept
    {
       libff::inhibit_profiling_info = true;
       libff::inhibit_profiling_counters = true;
        libff::alt_bn128_pp::init_public_params();
        return true;
    }();
    (void)s_initialized;
}

libff::bigint<libff::alt_bn128_q_limbs> Snark::ToLibsnarkBigint(const std::string& in_x) {
    if (in_x.size() != 32) {
        throw std::invalid_argument("ToLibsnarkBigint: expected 32 bytes");
    }
    libff::bigint<libff::alt_bn128_q_limbs> b;
    auto const N = b.N;
    constexpr size_t L = sizeof(b.data[0]);
    static_assert(sizeof(mp_limb_t) == L, "Unexpected limb size in libff::bigint.");
    // Default-constructed bigint may not clear all limbs; |= would leave garbage bits.
    for (size_t k = 0; k < N; ++k) {
        b.data[k] = 0;
    }
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < L; j++) {
            // std::string::operator[] is char; on Linux char is often signed — cast so bytes >= 0x80
            // are not sign-extended into mp_limb_t (breaks Fq decode for encoded curve points).
            uint8_t const u = static_cast<uint8_t>(static_cast<unsigned char>(in_x[i * L + j]));
            b.data[N - 1 - i] |= mp_limb_t(u) << (8 * (L - 1 - j));
        }
    }

    return b;
}

std::string Snark::FromLibsnarkBigint(libff::bigint<libff::alt_bn128_q_limbs> const& b) {
    const size_t N = static_cast<size_t>(b.N);
    const size_t L = sizeof(b.data[0]);
    static_assert(sizeof(mp_limb_t) == L, "Unexpected limb size in libff::bigint.");
    std::string out_x;
    out_x.resize(32);
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < L; j++) {
            out_x[i * L + j] = uint8_t(b.data[N - 1 - i] >> (8 * (L - 1 - j)));
        }
    }

    return out_x;
}

libff::alt_bn128_Fq Snark::DecodeFqElement(const std::string& data) {
    if (data.size() < 32) {
        throw std::invalid_argument("DecodeFqElement: need at least 32 bytes");
    }
    libff::bigint<libff::alt_bn128_q_limbs> const b = ToLibsnarkBigint(data.substr(0, 32));
    libff::alt_bn128_Fq const fq(b);
    // EIP-196: encoding must be the unique integer in [0, p); use libff reduction, not a hand limb compare.
    if (!(fq.as_bigint() == b)) {
        throw std::runtime_error("alt_bn128 Fq not canonical (< field modulus)");
    }
    return fq;
}

libff::alt_bn128_G1 Snark::DecodePointG1(const std::string& data) {
    if (data.size() < 64) {
        throw std::invalid_argument("DecodePointG1: need at least 64 bytes");
    }
    libff::alt_bn128_Fq x = DecodeFqElement(data.substr(0, 32));
    libff::alt_bn128_Fq y = DecodeFqElement(data.substr(32, 32));
    if (x == libff::alt_bn128_Fq::zero() && y == libff::alt_bn128_Fq::zero()) {
        return libff::alt_bn128_G1::zero();
    }

    libff::alt_bn128_G1 p(x, y, libff::alt_bn128_Fq::one());
    if (!p.is_well_formed()) {
        throw std::runtime_error("DecodePointG1: coordinates not on curve");
    }

    return p;
}

std::string Snark::EncodePointG1(libff::alt_bn128_G1 p) {
    if (p.is_zero()) {
        return std::string(64, 0);
    }

    p.to_affine_coordinates();
    return FromLibsnarkBigint(p.X.as_bigint()) + FromLibsnarkBigint(p.Y.as_bigint());
}

libff::alt_bn128_Fq2 Snark::DecodeFq2Element(const std::string& data) {
    if (data.size() < 64) {
        throw std::invalid_argument("DecodeFq2Element: need at least 64 bytes");
    }
    return libff::alt_bn128_Fq2(
        DecodeFqElement(data.substr(32, 32)),
        DecodeFqElement(data.substr(0, 32)));
}

libff::alt_bn128_G2 Snark::DecodePointG2(const std::string& data) {
    if (data.size() < 128) {
        throw std::invalid_argument("DecodePointG2: need at least 128 bytes");
    }
    libff::alt_bn128_Fq2 const x = DecodeFq2Element(data);
    libff::alt_bn128_Fq2 const y = DecodeFq2Element(data.substr(64, data.size() - 64));
    if (x == libff::alt_bn128_Fq2::zero() && y == libff::alt_bn128_Fq2::zero())
        return libff::alt_bn128_G2::zero();
    libff::alt_bn128_G2 p(x, y, libff::alt_bn128_Fq2::one());
    if (!p.is_well_formed()) {
        throw std::runtime_error("DecodePointG2: coordinates not on curve");
    }

    return p;
}

};  // namespace bignum

};  // namespace shardora
