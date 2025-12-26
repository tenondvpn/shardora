#include "bls/dkg_cache.h"
#include "common/global_info.h"

namespace shardora {
namespace bls {

DkgCache::DkgCache(std::shared_ptr<protos::PrefixDb>& prefix_db)
        : prefix_db_(prefix_db) {}

DkgCache::~DkgCache() {}

void DkgCache::Init(uint32_t local_index, common::Members& members, uint32_t network_id) {
    for (uint32_t peer_index = 0; peer_index < members.size(); ++peer_index) {
        if (!GetSwapKey(
                network_id,
                local_index,
                (*members[peer_index]).id,
                peer_index,
                nullptr)) {
            SHARDORA_DEBUG("failed init dkg cache hit swap key: local_index: %u, peer_index: %u, id: %s",
                local_index, peer_index,
                common::Encode::HexEncode((*members[peer_index]).id).c_str());
        } else {
            SHARDORA_DEBUG("success init dkg cache hit swap key: local_index: %u, peer_index: %u, id: %s",
                local_index, peer_index,
                common::Encode::HexEncode((*members[peer_index]).id).c_str());
        }

        if (GetBlsVerifyG2((*members[peer_index]).id, nullptr)) {
            SHARDORA_DEBUG("success init dkg cache hit bls verify g2: local_index: %u, peer_index: %u, id: %s",
                local_index, peer_index,
                common::Encode::HexEncode((*members[peer_index]).id).c_str());
        } else {
            SHARDORA_DEBUG("failed init dkg cache hit bls verify g2: local_index: %u, peer_index: %u, id: %s",
                local_index, peer_index,
                common::Encode::HexEncode((*members[peer_index]).id).c_str());
        }
    }
}

// void DkgCache::SetSwapKey(
//         uint32_t network_id,
//         uint32_t local_member_index,
//         const std::string& id,
//         uint32_t from_member_index,
//         const std::string& secret_key_str) {
//     prefix_db_->SaveSwapKey(
//         network_id,
//         local_member_index,
//         id,
//         from_member_index,
//         secret_key_str);

//     SwapKeyCacheKey key{elect_height, from_member_index, local_member_index, network_id};
//     swap_keys_cache_[key] = secret_key_str;
// }

bool DkgCache::GetSwapKey(
        uint32_t network_id,
        uint32_t local_index,
        const std::string& id,
        uint32_t peer_index,
        std::string* secret_key_str) {
    SwapKeyCacheKey key{id, peer_index, local_index, network_id};
    auto iter = swap_keys_cache_.find(key);
    if (iter != swap_keys_cache_.end()) {
        if (secret_key_str != nullptr) {
            *secret_key_str = iter->second;
        }

        return true;
    }

    SHARDORA_DEBUG("init dkg cache miss swap key: local_index: %u, peer_index: %u, id: %s",
        local_index, peer_index,
        common::Encode::HexEncode(id.c_str()));
    std::string tmp_secret_key_str;
    if (!prefix_db_->GetSwapKey(
            network_id,
            local_index,
            id,
            peer_index,
            &tmp_secret_key_str)) {
        SHARDORA_DEBUG("init dkg cache miss swap key: local_index: %u, peer_index: %u, id: %s",
            local_index, peer_index,
            common::Encode::HexEncode(id.c_str()));
        return false;
    }

    if (secret_key_str != nullptr) {
        *secret_key_str = iter->second;
    }
    swap_keys_cache_[key] = tmp_secret_key_str;
    return true;
}

bool DkgCache::GetBlsVerifyG2(
        const std::string& id,
        libff::alt_bn128_G2* verfy_req_g2) {
    auto it = verify_g2_cache_.find(id);
    if (it != verify_g2_cache_.end()) {
        if (verfy_req_g2 != nullptr) {
            *verfy_req_g2 = it->second;
        }
        return true;
    }

    bls::protobuf::VerifyVecBrdReq req;
    auto res = dkg_cache_->GetBlsVerifyG2((*members_)[peer_mem_index]->id, &req);
    if (!res) {
        SHARDORA_WARN("get verify g2 failed local: %d, %lu, %u",
            local_member_index_, elect_hegiht_, peer_mem_index);
        return libff::alt_bn128_G2::zero();
    }

    auto& item = req.verify_vec(0);
    auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
    auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
    auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
    auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
    auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
    auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
    auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
    auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
    auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
    *changed_idx = req.change_idx();
    auto g2 = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
    if (verfy_req != nullptr) {
        *verfy_req_g2 = g2;
    }

    verify_g2_cache_[id] = g2;
    return true;
}

}  // namespace bls
}  // namespace shardora