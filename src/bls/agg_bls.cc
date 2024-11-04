#include "bls/agg_bls.h"
#include <bls/bls.h>
#include <bls/bls_utils.h>
#include <common/bitmap.h>
#include <common/encode.h>
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
        const libff::alt_bn128_Fr &sec_key,
        const std::string &str_hash,
        libff::alt_bn128_G1* signature) {
    // NOTICE: 此处不能使用 Thresholdutils::HashtoG1
    auto hex_str = common::Encode::HexEncode(str_hash);
    *signature = libBLS::Bls::CoreSignAggregated(hex_str, sec_key);
}
    
void AggBls::Aggregate(
        const std::vector<libff::alt_bn128_G1>& sigs,
        libff::alt_bn128_G1* signature) {
    *signature = libBLS::Bls::Aggregate(sigs);
}

// Aggregateverify 不同的消息，没办法聚合公钥，比较慢，验证 e(P1, Q1)e(P2, Q2)... = e(G, S)
bool AggBls::AggregateVerify(
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::vector<std::string>& str_hashes,
        const libff::alt_bn128_G1& agg_sig) {
    return aggregatedVerification(str_hashes, agg_sig, pks);
}

// Fastaggregateverify 使用公钥聚合，快速验证 e(P, Q) = e(G, S)
bool AggBls::FastAggregateVerify(
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls::FastAggregateVerify(pks, common::Encode::HexEncode(str_hash), signature);
}

bool AggBls::CoreVerify(
        const libff::alt_bn128_G2& pk,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls::CoreVerify(pk, common::Encode::HexEncode(str_hash), signature);
    
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
