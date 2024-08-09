#pragma once

#include <common/bitmap.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
namespace shardora {

namespace bls {

struct AggregateSignature {
    libff::alt_bn128_G1 sig_;
    common::Bitmap participants_; // member indexes who submit signatures

    AggregateSignature(
            const libff::alt_bn128_G1& sig,
            const common::Bitmap& parts) : sig_(sig), participants_(parts) {}

    common::Bitmap participants() {
        return participants_;
    }

    libff::alt_bn128_G1 signature() {
        return sig_;
    }
};

class AggBls {
public:
    // sign a partial sig
    static void Sign(
            uint32_t t,
            uint32_t n,
            const libff::alt_bn128_Fr& sec_key,
            const libff::alt_bn128_G1& g1_hash,
            AggregateSignature* signature);

    // aggregate sigs to an agg_sig
    static void Aggregate(
            uint32_t t,
            uint32_t n,
            const std::vector<AggregateSignature>& sigs,
            AggregateSignature* agg_sig);

    // verify agg_sig for different messages 
    static bool AggregateVerify(
            uint32_t t,
            uint32_t n,
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::vector<libff::alt_bn128_G1>& g1_hashes,
            const libff::alt_bn128_G1& signature);    

    // verify agg_sig for a same message
    static bool FastAggregateVerify(
            uint32_t t,
            uint32_t n,
            const std::vector<libff::alt_bn128_G2>& pks,
            const libff::alt_bn128_G1& g1_hash,
            const libff::alt_bn128_G1& signature);

    // verify partial sig for a message
    static bool CoreVerify(
            uint32_t t,
            uint32_t n,
            const libff::alt_bn128_G2& public_key,
            const libff::alt_bn128_G1& g1_hash,
            const libff::alt_bn128_G1& signature);
};

}

}
