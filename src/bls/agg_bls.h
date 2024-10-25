#pragma once

#include <common/bitmap.h>
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
            return !sk_.is_zero() && !pk_.is_zero() && GetPublicKey(sk_) == pk_; 
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
    
    AggBls() {
        agg_bls_sk_ = libff::alt_bn128_Fr::zero();
    }
    ~AggBls() {}
    
    std::shared_ptr<KeyPair> GenerateKeyPair(
            uint32_t t, uint32_t n,
            std::shared_ptr<security::Security>& security,
            const std::shared_ptr<protos::PrefixDb>& prefix_db);
    std::shared_ptr<KeyPair> GetKeyPair(
            std::shared_ptr<security::Security>& security,
            const std::shared_ptr<protos::PrefixDb>& prefix_db);

    static libff::alt_bn128_G2 GetPublicKey(const libff::alt_bn128_Fr& sk) {
        return sk * libff::alt_bn128_G2::one();
    }
    // sign a partial sig
    static void Sign(
            uint32_t t, uint32_t n,
            const libff::alt_bn128_Fr& sec_key,
            const std::string& str_hash,
            libff::alt_bn128_G1* signature);

    // aggregate sigs to an agg_sig
    static void Aggregate(
            uint32_t t, uint32_t n,
            const std::vector<libff::alt_bn128_G1>& sigs,
            libff::alt_bn128_G1* signature);

    // verify agg_sig for different messages
    static bool AggregateVerify(
            uint32_t t, uint32_t n,
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::vector<std::string>& str_hashes,
            const libff::alt_bn128_G1& signature);    

    // verify agg_sig for a same message
    static bool FastAggregateVerify(
            uint32_t t, uint32_t n,
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::string& str_hash,
            const libff::alt_bn128_G1& signature);

    // verify partial sig for a message
    static bool CoreVerify(
            uint32_t t, uint32_t n,
            const libff::alt_bn128_G2& public_key,
            const std::string& str_hash,
            const libff::alt_bn128_G1& signature);

    static libff::alt_bn128_G2 AggregatePk(const std::vector<libff::alt_bn128_G2>& pks);

private:
    libff::alt_bn128_Fr agg_bls_sk_;

    libff::alt_bn128_G1 popProve();
    bool popVerify(
        const libff::alt_bn128_G2& public_key,
        const libff::alt_bn128_G1& proof);
};

}

}
