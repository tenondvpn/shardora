#pragma once

#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>

#include "common/utils.h"

namespace zjchain {

namespace common {

struct BftMember {
    BftMember(
        uint32_t nid,
        const std::string& in_id,
        const std::string& pkey,
        uint32_t idx,
        int32_t pool_mode_num)
        : net_id(nid),
        id(in_id),
        pubkey(pkey),
        index(idx),
        public_ip(0),
        pool_index_mod_num(pool_mode_num) {
        pool_index_mod_num = pool_mode_num;
    }

    uint32_t net_id;
    std::string id;
    std::string pubkey;
    uint32_t index;
    uint32_t public_ip;
    uint16_t public_port;
    std::string dht_key;
    volatile int32_t pool_index_mod_num;
    std::string backup_ecdh_key;
    std::string leader_ecdh_key;
    libff::alt_bn128_G2 bls_publick_key;
    bool valid_leader{ true };
    std::string peer_ecdh_key;
};

typedef std::shared_ptr<BftMember> BftMemberPtr;
typedef std::vector<BftMemberPtr> Members;
typedef std::shared_ptr<Members> MembersPtr;

};  // namespace common

};  // namespace zjchain
