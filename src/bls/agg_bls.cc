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
        // uint32_t t, uint32_t n,
        const libff::alt_bn128_Fr &sec_key,
        const std::string &str_hash,
        libff::alt_bn128_G1* signature) {
    // NOTICE: 此处不能使用 Thresholdutils::HashtoG1
    auto hex_str = common::Encode::HexEncode(str_hash);
    // libff::alt_bn128_G1 g1_hash = libBLS::Bls::Hashing(str_hash);
    *signature = libBLS::Bls::CoreSignAggregated(hex_str, sec_key);
}
    
void AggBls::Aggregate(
        // uint32_t t, uint32_t n,
        const std::vector<libff::alt_bn128_G1>& sigs,
        libff::alt_bn128_G1* signature) {
    *signature = libBLS::Bls::Aggregate(sigs);
}

bool AggBls::AggregateVerify(
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::vector<std::string>& str_hashes,
        const libff::alt_bn128_G1& signature) {
    std::vector<std::shared_ptr<std::array<uint8_t, 32>>> hash_bytes_arrs;
    auto agg_pk = AggregatePk(pks);
    for (const auto& str_hash : str_hashes) {
        auto hash_bytes_arr = std::make_shared< std::array< uint8_t, 32 > >();
        auto hex_str = common::Encode::HexEncode(str_hash);
        uint64_t bin_len;
        if ( !libBLS::ThresholdUtils::hex2carray( hex_str.c_str(), &bin_len, hash_bytes_arr->data() ) ) {
            throw std::runtime_error( "Invalid hash" );
        }        
        hash_bytes_arrs.push_back(hash_bytes_arr);
    }
    return libBLS::Bls::AggregatedVerification(hash_bytes_arrs, std::vector<libff::alt_bn128_G1>{signature}, agg_pk);
    // return aggregatedVerification(
    //         str_hashes,
    //         std::vector<libff::alt_bn128_G1>{signature},
    //         agg_pk);
}

bool AggBls::FastAggregateVerify(
        // uint32_t t, uint32_t n,
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    // return fastAggregateVerify(pks, str_hash, signature);
    return libBLS::Bls::FastAggregateVerify(pks, common::Encode::HexEncode(str_hash), signature);
}

bool AggBls::CoreVerify(
        const libff::alt_bn128_G2& pk,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    // return coreVerify(pk, str_hash, signature);
    return libBLS::Bls::CoreVerify(pk, common::Encode::HexEncode(str_hash), signature);
    
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
