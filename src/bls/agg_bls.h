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
    AggBls(
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<security::Security>& security) : db_(db), security_(security) {
        agg_keypair_ = std::make_pair(libff::alt_bn128_Fr::zero(), libff::alt_bn128_G2::zero());
    }
    ~AggBls() {}
    
    std::pair<libff::alt_bn128_Fr, libff::alt_bn128_G2> GenerateKeyPair(
            uint32_t t, uint32_t n, const std::shared_ptr<protos::PrefixDb>& prefix_db);
    std::pair<libff::alt_bn128_Fr, libff::alt_bn128_G2> GetKeyPair(const std::shared_ptr<protos::PrefixDb>& prefix_db);

    static libff::alt_bn128_G2 GetPublicKey(const libff::alt_bn128_Fr& sk) {
        return sk * libff::alt_bn128_G2::one();
    }
    // sign a partial sig
    void Sign(
            uint32_t t, uint32_t n,
            const libff::alt_bn128_Fr& sec_key,
            const std::string& str_hash,
            libff::alt_bn128_G1* signature);

    // aggregate sigs to an agg_sig
    void Aggregate(
            uint32_t t, uint32_t n,
            const std::vector<libff::alt_bn128_G1>& sigs,
            libff::alt_bn128_G1* signature);

    // verify agg_sig for different messages
    bool AggregateVerify(
            uint32_t t, uint32_t n,
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::vector<std::string>& str_hashes,
            const libff::alt_bn128_G1& signature);    

    // verify agg_sig for a same message
    bool FastAggregateVerify(
            uint32_t t, uint32_t n,
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::string& str_hash,
            const libff::alt_bn128_G1& signature);

    // verify partial sig for a message
    bool CoreVerify(
            uint32_t t, uint32_t n,
            const libff::alt_bn128_G2& public_key,
            const std::string& str_hash,
            const libff::alt_bn128_G1& signature);

    libff::alt_bn128_G2 AggregatePk(const std::vector<libff::alt_bn128_G2>& pks);

private:
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    libff::alt_bn128_Fr agg_bls_sk_;
};

}

}
