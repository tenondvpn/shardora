#include "bls/agg_bls.h"
#include <bls/bls.h>
#include <bls/bls_utils.h>
#include <common/bitmap.h>
#include <common/global_info.h>
#include <common/hash.h>
#include <tools/utils.h>

namespace shardora {

namespace bls {

std::pair<libff::alt_bn128_Fr, libff::alt_bn128_G2> AggBls::GetOrGenKeyPair() {
    auto bls_prikey = libff::alt_bn128_Fr::zero();
    if (agg_keypair_.first != libff::alt_bn128_Fr::zero() &&
        agg_keypair_.second != libff::alt_bn128_G2::zero() &&
        agg_keypair_.second == GetPublicKey(agg_keypair_.first)) {
        return agg_keypair_;
    }
    auto ok = prefix_db_->GetAggBlsPrikey(security_, &bls_prikey);
    if (ok && bls_prikey != libff::alt_bn128_Fr::zero()) {
        return std::make_pair(bls_prikey, GetPublicKey(bls_prikey));
    }
    
    auto keypair = libBLS::Bls(t_, n_).KeyGeneration();
    agg_keypair_ = keypair;
    prefix_db_->SaveAggBlsPrikey(security_, keypair.first);
    return keypair;
}

void AggBls::Sign(
        const libff::alt_bn128_Fr &sec_key,
        const std::string &str_hash,
        libff::alt_bn128_G1* signature) try {
    auto g1_hash = libBLS::Bls::Hashing(str_hash);
    libBLS::Bls bls_instance = libBLS::Bls(t_, n_);
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
        const std::vector<libff::alt_bn128_G1>& sigs,
        libff::alt_bn128_G1* signature) try {
    *signature = libBLS::Bls(t_, n_).Aggregate(sigs);
} catch (std::exception& e) {
    BLS_ERROR("agg bls aggregate message failed: %s", e.what());
    *signature = libff::alt_bn128_G1::zero();
}


bool AggBls::AggregateVerify(
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::vector<std::string>& str_hashes,
        const libff::alt_bn128_G1& signature) {
    std::vector<std::shared_ptr<std::array<uint8_t, 32>>> hash_byte_arr;
    for (const auto& str_hash : str_hashes) {
        auto h = common::HashValue(str_hash);
        hash_byte_arr.push_back(std::make_shared<std::array<uint8_t, 32>>(h.data));
    }
    auto agg_pk = AggregatePk(pks);
    return libBLS::Bls(t_, n_).AggregatedVerification(
            hash_byte_arr,
            std::vector<libff::alt_bn128_G1>{signature},
            agg_pk);
}


bool AggBls::FastAggregateVerify(
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls(t_, n_).FastAggregateVerify(pks, str_hash, signature);
}

bool AggBls::CoreVerify(
        const libff::alt_bn128_G2& pk,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls(t_, n_).CoreVerify(pk, str_hash, signature);     
}

libff::alt_bn128_G2 AggBls::AggregatePk(const std::vector<libff::alt_bn128_G2>& pks) {
    return std::accumulate(pks.begin(), pks.end(), libff::alt_bn128_G2::zero());
}
        

}

}
