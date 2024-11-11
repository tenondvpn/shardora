#pragma once

#include <bls/bls.h>
#include <common/bitmap.h>
#include <common/hash.h>
#include <common/utils.h>
#include <db/db.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <protos/prefix_db.h>
#include <unordered_set>

namespace shardora {

namespace bls {

class AggBls {
public:
    struct KeyPair {
        libff::alt_bn128_Fr sk_;
        libff::alt_bn128_G2 pk_;
        libff::alt_bn128_G1 proof_;

        KeyPair(const libff::alt_bn128_Fr& sk,
            const libff::alt_bn128_G2& pk,
            const libff::alt_bn128_G1& proof) : sk_(sk), pk_(pk), proof_(proof) {}

        inline bool IsValid() const {
            return !sk_.is_zero() &&
                sk_ != libff::alt_bn128_Fr::one() &&
                !pk_.is_zero() &&
                GetPublicKey(sk_) == pk_; 
        }

        inline libff::alt_bn128_Fr sk() {
            return sk_;
        }

        inline libff::alt_bn128_G2 pk() {
            return pk_;
        }

        inline libff::alt_bn128_G1 proof() {
            return proof_;
        }
    };

    static AggBls* Instance() {
        static AggBls ins;
        return &ins;
    }

    int Init(
            std::shared_ptr<protos::PrefixDb>& prefix_db,
            std::shared_ptr<security::Security>& security) {
        libBLS::ThresholdUtils::initCurve();
        
        prefix_db_ = prefix_db;
        security_ = security;

        auto bls_prikey = libff::alt_bn128_Fr::zero();
        auto ok = prefix_db->GetAggBlsPrikey(security, &bls_prikey);
        if (ok && bls_prikey != libff::alt_bn128_Fr::zero()) {
            agg_bls_sk_ = bls_prikey;
            return common::kCommonSuccess;
        }

        auto keypair = GenerateKeyPair();
        prefix_db->SaveAggBlsPrikey(security, keypair->sk());
        agg_bls_sk_ = keypair->sk();
        return common::kCommonSuccess;
    }

    static std::shared_ptr<KeyPair> GenerateKeyPair();    
    
    std::shared_ptr<KeyPair> GetKeyPair();

    static libff::alt_bn128_G2 GetPublicKey(const libff::alt_bn128_Fr& sk) {
        return sk * libff::alt_bn128_G2::one();
    }
    // sign a partial sig
    static void Sign(
            const libff::alt_bn128_Fr& sec_key,
            const std::string& str_hash,
            libff::alt_bn128_G1* signature);

    // aggregate sigs to an agg_sig
    static void Aggregate(
            const std::vector<libff::alt_bn128_G1>& sigs,
            libff::alt_bn128_G1* signature);

    // verify agg_sig for different messages
    static bool AggregateVerify(
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::vector<std::string>& str_hashes,
            const libff::alt_bn128_G1& signature);    

    // verify agg_sig for a same message
    static bool FastAggregateVerify(
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::string& str_hash,
            const libff::alt_bn128_G1& signature);

    // verify partial sig for a message
    static bool CoreVerify(
            const libff::alt_bn128_G2& public_key,
            const std::string& str_hash,
            const libff::alt_bn128_G1& signature);

    static bool PopVerify(
        const libff::alt_bn128_G2& public_key,
        const libff::alt_bn128_G1& proof);    

    static libff::alt_bn128_G1 PopProve(const libff::alt_bn128_Fr& sk);
private:
    libff::alt_bn128_Fr agg_bls_sk_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;

    AggBls() {
        agg_bls_sk_ = libff::alt_bn128_Fr::zero();        
    }
    ~AggBls() {}

    static bool aggregatedVerification(
            std::vector<std::string> str_hashes,
            const libff::alt_bn128_G1& agg_sig,
            const std::vector<libff::alt_bn128_G2> pks ) {
        if (str_hashes.size() != pks.size()) {
            return false;
        }

        auto right = libff::alt_bn128_GT::one();
        libff::alt_bn128_G1 aggregated_hash = libff::alt_bn128_G1::zero();
        for (uint32_t i = 0; i < pks.size(); i++) {
            auto pk = pks[i];
            if (!pk.is_well_formed()) {
                throw libBLS::ThresholdUtils::IsNotWellFormed( "Error, public key is invalid" );
            }

            if (!libBLS::ThresholdUtils::ValidateKey(pk)) {
                throw libBLS::ThresholdUtils::IsNotWellFormed( "Error, public key is not member of G2" );
            }

            auto hash_g1 = libBLS::ThresholdUtils::HashtoG1(common::Encode::HexEncode(str_hashes[i]));
            aggregated_hash = aggregated_hash + hash_g1;
            right = right * libff::alt_bn128_ate_reduced_pairing(hash_g1, pks[i]); 
        }

        return right == libff::alt_bn128_ate_reduced_pairing(agg_sig, libff::alt_bn128_G2::one());
    }    
};

}

}
