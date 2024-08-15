#pragma once

#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <memory>
#include <protos/elect.pb.h>
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

#define BLS_DEBUG(fmt, ...) ZJC_DEBUG("[bls]" fmt, ## __VA_ARGS__)
#define BLS_INFO(fmt, ...) ZJC_INFO("[bls]" fmt, ## __VA_ARGS__)
#define BLS_WARN(fmt, ...) ZJC_WARN("[bls]" fmt, ## __VA_ARGS__)
#define BLS_ERROR(fmt, ...) ZJC_ERROR("[bls]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace bls {

enum BlsErrorCode {
    kBlsSuccess = 0,
    kBlsError = 1,
};

static const float kBlsMaxExchangeMembersRatio = 0.70f;  // 90%

struct MaxBlsMemberItem {
    MaxBlsMemberItem(uint32_t c, const common::Bitmap& b)
        : count(c), bitmap(b) {}
    uint32_t count;
    common::Bitmap bitmap;
};

struct BlsFinishItem {
    libff::alt_bn128_G2 all_public_keys[common::kEachShardMaxNodeCount] = { libff::alt_bn128_G2::zero() };
    libff::alt_bn128_G1 all_bls_signs[common::kEachShardMaxNodeCount] = { libff::alt_bn128_G1::zero() };
    libff::alt_bn128_G2 all_common_public_keys[common::kEachShardMaxNodeCount] = { libff::alt_bn128_G2::zero() };
    uint32_t max_finish_count{ 0 };
    std::string max_finish_hash;
    std::unordered_map<std::string, std::shared_ptr<MaxBlsMemberItem>> max_bls_members;
    std::unordered_map<std::string, uint32_t> max_public_pk_map;
    std::unordered_map<std::string, libff::alt_bn128_G2> common_pk_map;
    std::vector<libff::alt_bn128_G1> verify_t_signs;
    std::vector<libff::alt_bn128_G1> verified_valid_signs;
    std::vector<size_t> verified_valid_index;
    bool verified[common::kEachShardMaxNodeCount] = { false };
    bool success_verified{ false };
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

static elect::protobuf::BlsPublicKey* BlsPublicKey2Proto(const libff::alt_bn128_G2& bls_pk) {
    elect::protobuf::BlsPublicKey* bls_pk_proto{};
    auto bls_pk_strs = std::make_shared<BLSPublicKey>(bls_pk)->toString();
    bls_pk_proto->set_x_c0(bls_pk_strs->at(0));
    bls_pk_proto->set_x_c1(bls_pk_strs->at(1));
    bls_pk_proto->set_y_c0(bls_pk_strs->at(2));
    bls_pk_proto->set_y_c1(bls_pk_strs->at(3));
    return bls_pk_proto;
}

static std::shared_ptr<libff::alt_bn128_G2> Proto2BlsPublicKey(const elect::protobuf::BlsPublicKey& bls_pk_proto) {
    auto pk_str_vec = std::vector<std::string>{
        bls_pk_proto.x_c0(),
        bls_pk_proto.x_c1(),
        bls_pk_proto.y_c0(),
        bls_pk_proto.y_c1(),
    };
    auto bls_pk = BLSPublicKey(std::make_shared<std::vector<std::string>>(pk_str_vec));
    return bls_pk.getPublicKey();
}

}  // namespace bls

}  // namespace shardora
