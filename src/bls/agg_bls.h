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
    AggBls(uint32_t t, uint32_t n,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<security::Security>& security) : t_(t), n_(n), db_(db), security_(security) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        agg_keypair_ = std::make_pair(libff::alt_bn128_Fr::zero(), libff::alt_bn128_G2::zero());
    }
    ~AggBls() {}
    
    std::pair<libff::alt_bn128_Fr, libff::alt_bn128_G2> GetOrGenKeyPair();

    libff::alt_bn128_G2 GetPublicKey(const libff::alt_bn128_Fr& sk) {
        return sk * libff::alt_bn128_G2::one();
    }
    // sign a partial sig
    void Sign(
            const libff::alt_bn128_Fr& sec_key,
            const std::string& str_hash,
            libff::alt_bn128_G1* signature);

    // aggregate sigs to an agg_sig
    void Aggregate(
            const std::vector<libff::alt_bn128_G1>& sigs,
            libff::alt_bn128_G1* signature);

    // verify agg_sig for different messages
    bool AggregateVerify(
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::vector<std::string>& str_hashes,
            const libff::alt_bn128_G1& signature);    

    // verify agg_sig for a same message
    bool FastAggregateVerify(
            const std::vector<libff::alt_bn128_G2>& pks,
            const std::string& str_hash,
            const libff::alt_bn128_G1& signature);

    // verify partial sig for a message
    bool CoreVerify(
            const libff::alt_bn128_G2& public_key,
            const std::string& str_hash,
            const libff::alt_bn128_G1& signature);

    libff::alt_bn128_G2 AggregatePk(const std::vector<libff::alt_bn128_G2>& pks);

private:
    uint32_t t_;
    uint32_t n_;

    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::pair<libff::alt_bn128_Fr, libff::alt_bn128_G2> agg_keypair_;
};

}

}
