#include "bls/agg_bls.h"
#include <bls/bls.h>
#include <bls/bls_utils.h>
#include <common/bitmap.h>

namespace shardora {

namespace bls {

void AggBls::Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr &sec_key,
        const libff::alt_bn128_G1 &g1_hash,
        libff::alt_bn128_G1* signature) {
    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        *signature = bls_instance.Signing(g1_hash, sec_key);
        BLS_DEBUG("agg bls sign message success: %s, hash: %s, %s, %s",
            libBLS::ThresholdUtils::fieldElementToString(sec_key).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(g1_hash.X).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(g1_hash.Y).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(g1_hash.Z).c_str());
    } catch (std::exception& e) {
        BLS_ERROR("agg bls sign message failed: %s", e.what());
        *signature = libff::alt_bn128_G1::zero();
    }
}

}

}
