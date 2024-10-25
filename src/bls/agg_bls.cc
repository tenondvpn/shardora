#include "bls/agg_bls.h"
#include <bls/bls.h>
#include <bls/bls_utils.h>
#include <common/bitmap.h>
#include <common/global_info.h>
#include <common/hash.h>
#include <tools/utils.h>

namespace shardora {

namespace bls {

std::shared_ptr<AggBls::KeyPair> AggBls::GenerateKeyPair(
        uint32_t t, uint32_t n,
        std::shared_ptr<security::Security>& security,
        const std::shared_ptr<protos::PrefixDb>& prefix_db) {
    auto keypair = libBLS::Bls(t, n).KeyGeneration();
    agg_bls_sk_ = keypair.first;
    prefix_db->SaveAggBlsPrikey(security, keypair.first);
    
    return std::make_shared<AggBls::KeyPair>(
            keypair.first,
            keypair.second,
            popProve());
}

std::shared_ptr<AggBls::KeyPair> AggBls::GetKeyPair(
        std::shared_ptr<security::Security>& security,
        const std::shared_ptr<protos::PrefixDb>& prefix_db) {
    if (agg_bls_sk_ != libff::alt_bn128_Fr::zero()) {
        return std::make_shared<AggBls::KeyPair>(
                agg_bls_sk_,
                GetPublicKey(agg_bls_sk_),
                popProve());
    }

    auto bls_prikey = libff::alt_bn128_Fr::zero();
    auto ok = prefix_db->GetAggBlsPrikey(security, &bls_prikey);
    if (ok && bls_prikey != libff::alt_bn128_Fr::zero()) {
        agg_bls_sk_ = bls_prikey;
        return std::make_shared<AggBls::KeyPair>(bls_prikey, GetPublicKey(bls_prikey), popProve());
    }

    return std::make_shared<AggBls::KeyPair>(
            libff::alt_bn128_Fr::zero(),
            libff::alt_bn128_G2::zero(),
            libff::alt_bn128_G1::zero());
}

void AggBls::Sign(
        uint32_t t, uint32_t n,
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
        uint32_t t, uint32_t n,
        const std::vector<libff::alt_bn128_G1>& sigs,
        libff::alt_bn128_G1* signature) try {
    *signature = libBLS::Bls(t, n).Aggregate(sigs);
} catch (std::exception& e) {
    BLS_ERROR("agg bls aggregate message failed: %s", e.what());
    *signature = libff::alt_bn128_G1::zero();
}


bool AggBls::AggregateVerify(
        uint32_t t, uint32_t n,
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
        uint32_t t, uint32_t n,
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls(t, n).FastAggregateVerify(pks, str_hash, signature);
}

bool AggBls::CoreVerify(
        uint32_t t, uint32_t n,
        const libff::alt_bn128_G2& pk,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls(t, n).CoreVerify(pk, str_hash, signature);     
}

libff::alt_bn128_G2 AggBls::AggregatePk(const std::vector<libff::alt_bn128_G2>& pks) {
    return std::accumulate(pks.begin(), pks.end(), libff::alt_bn128_G2::zero());
}

libff::alt_bn128_G1 AggBls::popProve() {
    return libBLS::Bls::PopProve(agg_bls_sk_);
}

bool AggBls::PopVerify(
        const libff::alt_bn128_G2& public_key,
        const libff::alt_bn128_G1& proof) {
    return libBLS::Bls::PopVerify(public_key, proof);
}

}

}
