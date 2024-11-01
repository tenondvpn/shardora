#include "bls/agg_bls.h"
#include <bls/bls.h>
#include <bls/bls_utils.h>
#include <common/bitmap.h>
#include <common/global_info.h>
#include <common/hash.h>
#include <tools/utils.h>

namespace shardora {

namespace bls {

std::shared_ptr<AggBls::KeyPair> AggBls::GenerateKeyPair() {
    auto keypair = libBLS::Bls::KeyGeneration();
    return std::make_shared<AggBls::KeyPair>(
            keypair.first,
            keypair.second,
            PopProve(keypair.first));
}

std::shared_ptr<AggBls::KeyPair> AggBls::GetKeyPair() {    
    return std::make_shared<AggBls::KeyPair>(
            agg_bls_sk_,
            GetPublicKey(agg_bls_sk_),
            PopProve(agg_bls_sk_));
}

void AggBls::Sign(
        // uint32_t t, uint32_t n,
        const libff::alt_bn128_Fr &sec_key,
        const std::string &str_hash,
        libff::alt_bn128_G1* signature) {
    // NOTICE: 此处不能使用 Thresholdutils::HashtoG1
    libff::alt_bn128_G1 g1_hash = libBLS::Bls::Hashing(str_hash);
    *signature = libBLS::Bls::Signing(g1_hash, sec_key);
}
    
void AggBls::Aggregate(
        // uint32_t t, uint32_t n,
        const std::vector<libff::alt_bn128_G1>& sigs,
        libff::alt_bn128_G1* signature) {
    *signature = libBLS::Bls::Aggregate(sigs);
}

bool AggBls::AggregateVerify(
        // uint32_t t, uint32_t n,
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::vector<std::string>& str_hashes,
        const libff::alt_bn128_G1& signature) {
    std::vector<std::shared_ptr<std::array<uint8_t, 32>>> hash_byte_arr;
    for (const auto& str_hash : str_hashes) {
        auto h = common::HashValue(str_hash);
        hash_byte_arr.push_back(std::make_shared<std::array<uint8_t, 32>>(h.data));
    }
    auto agg_pk = AggregatePk(pks);
    return libBLS::Bls::AggregatedVerification(
            hash_byte_arr,
            std::vector<libff::alt_bn128_G1>{signature},
            agg_pk);
}

bool AggBls::FastAggregateVerify(
        // uint32_t t, uint32_t n,
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return fastAggregateVerify(pks, str_hash, signature);
}

bool AggBls::CoreVerify(
        const libff::alt_bn128_G2& pk,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return coreVerify(pk, str_hash, signature);     
}

libff::alt_bn128_G2 AggBls::AggregatePk(const std::vector<libff::alt_bn128_G2>& pks) {
    return std::accumulate(pks.begin(), pks.end(), libff::alt_bn128_G2::zero());
}

libff::alt_bn128_G1 AggBls::PopProve(const libff::alt_bn128_Fr& sk) {
    return libBLS::Bls::PopProve(sk);
}

bool AggBls::PopVerify(
        const libff::alt_bn128_G2& public_key,
        const libff::alt_bn128_G1& proof) {
    return libBLS::Bls::PopVerify(public_key, proof);
}

}

}
