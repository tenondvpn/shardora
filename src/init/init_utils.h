#pragma once

#include <queue>

#include <libbls/bls/BLSPrivateKey.h>
#include <libbls/bls/BLSPrivateKeyShare.h>
#include <libbls/bls/BLSPublicKey.h>
#include <libbls/bls/BLSPublicKeyShare.h>
#include <libbls/tools/utils.h>

#include "common/encode.h"
#include "common/log.h"
#include "common/node_members.h"
#include "common/utils.h"
#include "protos/bls.pb.h"

#define INIT_DEBUG(fmt, ...) SHARDORA_DEBUG("[init]" fmt, ## __VA_ARGS__)
#define INIT_INFO(fmt, ...) SHARDORA_DEBUG("[init]" fmt, ## __VA_ARGS__)
#define INIT_WARN(fmt, ...) SHARDORA_WARN("[init]" fmt, ## __VA_ARGS__)
#define INIT_ERROR(fmt, ...) SHARDORA_ERROR("[init]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace init {

enum InitErrorCode {
    kInitSuccess = 0,
    kInitError = 1,
    kInitWaitingForPrivateKey = 2,
};

struct RotatitionVersionInfo {
    std::set<uint32_t> handled_set;
    std::map<uint32_t, uint32_t> count_map;
};

struct RotatitionLeaders {
    RotatitionLeaders() : version(-1), invalid_pool_count(0), now_leader_idx(0) {}
    int32_t version;
    std::vector<uint32_t> rotation_leaders;
    uint32_t invalid_pool_count;
    uint32_t now_leader_idx;
    std::map<uint32_t, RotatitionVersionInfo> version_with_count;
};

struct LeaderRotationInfo {
    LeaderRotationInfo() : elect_height(0), members(nullptr) {
        memset(rotation_used, 0, sizeof(rotation_used));
    }

    uint64_t elect_height;
    uint32_t member_count;
    std::vector<RotatitionLeaders> rotations;
    std::set<uint32_t> invalid_leaders;
    common::MembersPtr members;
    uint64_t tm_block_tm;
    uint32_t local_member_index;
    bool rotation_used[common::kEachShardMaxNodeCount];
};

struct GenisisNodeInfo {
    std::string prikey;
    std::string pubkey;
    std::string id;
    std::vector<libff::alt_bn128_Fr> polynomial;
    libff::alt_bn128_Fr bls_prikey;
    libff::alt_bn128_G2 bls_pubkey;
    std::vector<libff::alt_bn128_G2> verification;
    bls::protobuf::JoinElectInfo g2_val;
    uint64_t nonce;
};

typedef std::shared_ptr<GenisisNodeInfo> GenisisNodeInfoPtr;


}  // namespace init

}  // namespace shardora
