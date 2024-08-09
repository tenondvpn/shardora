#include "bls/agg_bls.h"
#include <bls/bls.h>
#include <bls/bls_utils.h>
#include <common/bitmap.h>
#include <common/hash.h>
#include <tools/utils.h>

namespace shardora {

namespace bls {

void AggBls::Sign(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_Fr &sec_key,
        const std::string &str_hash,
        libff::alt_bn128_G1* signature) try {
    auto g1_hash = libBLS::Bls::Hashing(str_hash);
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
    
void AggBls::Aggregate(
        uint32_t t,
        uint32_t n,
        const std::vector<libff::alt_bn128_G1>& sigs,
        libff::alt_bn128_G1* signature) try {
    *signature = libBLS::Bls(t, n).Aggregate(sigs);
} catch (std::exception& e) {
    BLS_ERROR("agg bls aggregate message failed: %s", e.what());
    *signature = libff::alt_bn128_G1::zero();
}


bool AggBls::AggregateVerify(
        uint32_t t,
        uint32_t n,
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::vector<std::string>& str_hashes,
        const libff::alt_bn128_G1& signature) {
    std::vector<std::shared_ptr<std::array<uint8_t, 32>>> hash_byte_arr;
    for (const auto& str_hash : str_hashes) {
        auto h = common::HashValue(str_hash);
        hash_byte_arr.push_back(std::make_shared<std::array<uint8_t, 32>>(h.data));
    }
    auto agg_pk = AggregatePk(pks);
    return libBLS::Bls(t, n).AggregatedVerification(
            hash_byte_arr,
            std::vector<libff::alt_bn128_G1>{signature},
            agg_pk);
}


bool AggBls::FastAggregateVerify(
        uint32_t t,
        uint32_t n,
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls(t, n).FastAggregateVerify(pks, str_hash, signature);
}

bool AggBls::CoreVerify(
        uint32_t t,
        uint32_t n,
        const libff::alt_bn128_G2& pk,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls(t, n).CoreVerify(pk, str_hash, signature);     
}

libff::alt_bn128_G2 AggBls::AggregatePk(const std::vector<libff::alt_bn128_G2>& pks) {
    return std::accumulate( pks.begin(), pks.end(), libff::alt_bn128_G2::zero() );
}
        

}

}
