#pragma once

#include <memory>
#include <unordered_map>

#include <libbls/bls/BLSPrivateKey.h>
#include <libbls/bls/BLSPrivateKeyShare.h>
#include <libbls/bls/BLSPublicKey.h>
#include <libbls/bls/BLSPublicKeyShare.h>
#include <libbls/tools/utils.h>
#include <dkg/dkg.h>

#include "common/bitmap.h"
#include "common/log.h"
#include "common/limit_heap.h"
#include "common/node_members.h"
#include "common/utils.h"
#include "protos/elect.pb.h"

#define BLS_DEBUG(fmt, ...) SHARDORA_DEBUG("[bls]" fmt, ## __VA_ARGS__)
#define BLS_INFO(fmt, ...) SHARDORA_INFO("[bls]" fmt, ## __VA_ARGS__)
#define BLS_WARN(fmt, ...) SHARDORA_WARN("[bls]" fmt, ## __VA_ARGS__)
#define BLS_ERROR(fmt, ...) SHARDORA_ERROR("[bls]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace bls {

enum BlsErrorCode {
    kBlsSuccess = 0,
    kBlsError = 1,
};

static const float kBlsMaxExchangeMembersRatio = 0.8f;  // 80%

struct MaxBlsMemberItem {
    MaxBlsMemberItem(uint32_t c, const common::Bitmap& b)
        : count(c), bitmap(b) {}
    uint32_t count;
    common::Bitmap bitmap;
};

class BlsFinishItem {
public:
    BlsFinishItem() : max_finish_count(0), success_verified(false) {
        memset(verified, 0, sizeof(verified));
    }

    libff::alt_bn128_G2 all_public_keys[common::kEachShardMaxNodeCount] = { libff::alt_bn128_G2::zero() };
    libff::alt_bn128_G1 all_bls_signs[common::kEachShardMaxNodeCount] = { libff::alt_bn128_G1::zero() };
    libff::alt_bn128_G2 all_common_public_keys[common::kEachShardMaxNodeCount] = { libff::alt_bn128_G2::zero() };
    uint32_t max_finish_count;
    std::string max_finish_hash;
    std::unordered_map<std::string, std::shared_ptr<MaxBlsMemberItem>> max_bls_members;
    std::unordered_map<std::string, uint32_t> max_public_pk_map;
    std::unordered_map<std::string, libff::alt_bn128_G2> common_pk_map;
    std::vector<libff::alt_bn128_G1> verify_t_signs;
    std::vector<libff::alt_bn128_G1> verified_valid_signs;
    std::vector<size_t> verified_valid_index;
    bool verified[common::kEachShardMaxNodeCount];
    bool success_verified;
};

typedef std::shared_ptr<BlsFinishItem> BlsFinishItemPtr;

struct TimeBlockItem {
    uint64_t lastest_time_block_tm;
    uint64_t latest_time_block_height;
    uint64_t vss_random;
};

struct ElectItem {
    uint64_t height;
    common::MembersPtr members;
};

static inline bool IsValidBigInt(const std::string& big_int) {
    for (size_t i = 0; i < big_int.size(); ++i)
    {
        if (big_int[i] >= '0' && big_int[i] <= '9') {
            continue;
        }

        return false;
    }

    return true;
}

static std::shared_ptr<elect::protobuf::BlsPublicKey> BlsPublicKey2Proto(
        const libff::alt_bn128_G2& bls_pk) {
    auto bls_pk_proto = std::make_shared<elect::protobuf::BlsPublicKey>();
    auto bls_pk_strs = BLSPublicKey(bls_pk).toString();
    if (bls_pk_strs->size() >= 4) {
        bls_pk_proto->set_x_c0(bls_pk_strs->at(0));
        bls_pk_proto->set_x_c1(bls_pk_strs->at(1));
        bls_pk_proto->set_y_c0(bls_pk_strs->at(2));
        bls_pk_proto->set_y_c1(bls_pk_strs->at(3));
        return bls_pk_proto;        
    }
    return nullptr;
}

static std::shared_ptr<libff::alt_bn128_G2> Proto2BlsPublicKey(
        const elect::protobuf::BlsPublicKey& bls_pk_proto) {
    auto pk_str_vec = std::vector<std::string>{
        bls_pk_proto.x_c0(),
        bls_pk_proto.x_c1(),
        bls_pk_proto.y_c0(),
        bls_pk_proto.y_c1(),
    };
    auto bls_pk = BLSPublicKey(std::make_shared<std::vector<std::string>>(pk_str_vec));
    return bls_pk.getPublicKey();
}

static std::shared_ptr<elect::protobuf::BlsPopProof> BlsPopProof2Proto(
        const libff::alt_bn128_G1 &proof) {
    auto proof_proto = std::make_shared<elect::protobuf::BlsPopProof>();
    proof_proto->set_sign_x(libBLS::ThresholdUtils::fieldElementToString(proof.X));
    proof_proto->set_sign_y(libBLS::ThresholdUtils::fieldElementToString(proof.Y));
    proof_proto->set_sign_z(libBLS::ThresholdUtils::fieldElementToString(proof.Z));
    return proof_proto;
}

static std::shared_ptr<libff::alt_bn128_G1> Proto2BlsPopProof(
        const elect::protobuf::BlsPopProof& proof_proto) {
    auto proof = std::make_shared<libff::alt_bn128_G1>();

    try {
        if (proof_proto.sign_x() != "") {
            proof->X = libff::alt_bn128_Fq(proof_proto.sign_x().c_str());
        }
        if (proof_proto.sign_y() != "") {
            proof->Y = libff::alt_bn128_Fq(proof_proto.sign_y().c_str());
        }
        if (proof_proto.sign_z() != "") {
            proof->Z = libff::alt_bn128_Fq(proof_proto.sign_z().c_str());
        }        
    } catch (...) {
        return nullptr;
    }
    return proof;
}

}  // namespace bls

}  // namespace shardora
