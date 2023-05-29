#pragma once

#include <libbls/bls/BLSPrivateKey.h>
#include <libbls/bls/BLSPrivateKeyShare.h>
#include <libbls/bls/BLSPublicKey.h>
#include <libbls/bls/BLSPublicKeyShare.h>
#include <libbls/tools/utils.h>

#include "common/encode.h"
#include "common/log.h"
#include "common/node_members.h"
#include "common/utils.h"

#define INIT_DEBUG(fmt, ...) ZJC_DEBUG("[init]" fmt, ## __VA_ARGS__)
#define INIT_INFO(fmt, ...) ZJC_INFO("[init]" fmt, ## __VA_ARGS__)
#define INIT_WARN(fmt, ...) ZJC_WARN("[init]" fmt, ## __VA_ARGS__)
#define INIT_ERROR(fmt, ...) ZJC_ERROR("[init]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace init {

enum InitErrorCode {
    kInitSuccess = 0,
    kInitError = 1,
};

struct RotatitionLeaders {
    RotatitionLeaders() : now_rotation_idx(0), invalid_pool_count(0), now_leader_idx(0) {}
    std::vector<uint32_t> rotation_leaders;
    uint32_t now_rotation_idx;
    uint32_t invalid_pool_count;
    uint32_t now_leader_idx;
};

struct LeaderRotationInfo {
    LeaderRotationInfo() : elect_height(0), members(nullptr) {}
    uint64_t elect_height;
    uint32_t member_count;
    std::vector<RotatitionLeaders> rotations;
    std::set<uint32_t> invalid_leaders;
    common::MembersPtr members;
};

struct GenisisNodeInfo {
    std::string prikey;
    std::string pubkey;
    std::string id;
    std::string bls_prikey;
    std::vector<libff::alt_bn128_Fr> polynomial;
    libff::alt_bn128_Fr bls_prikey;
    libff::alt_bn128_G2 bls_pubkey;
    std::vector<libff::alt_bn128_G2> verification;
    std::string check_hash;
    bls::protobuf::JoinElectInfo join_info;
};

typedef std::shared_ptr<GenisisNodeInfo> GenisisNodeInfoPtr;

}  // namespace init

}  // namespace zjchain
