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
                (*members[peer_index])->id,
                peer_index,
                nullptr)) {
            SHARDORA_DEBUG("failed init dkg cache hit swap key: local_index: %u, peer_index: %u, id: %s",
                local_index, peer_index,
                common::Encode::HexEncode((*members[peer_index])->id).c_str());
        } else {
            SHARDORA_DEBUG("success init dkg cache hit swap key: local_index: %u, peer_index: %u, id: %s",
                local_index, peer_index,
                common::Encode::HexEncode((*members[peer_index])->id).c_str());
        }

        if (GetBlsVerifyG2((*members[peer_index])->id, nullptr)) {
            SHARDORA_DEBUG("success init dkg cache hit bls verify g2: local_index: %u, peer_index: %u, id: %s",
                local_index, peer_index,
                common::Encode::HexEncode((*members[peer_index])->id).c_str());
        } else {
            SHARDORA_DEBUG("failed init dkg cache hit bls verify g2: local_index: %u, peer_index: %u, id: %s",
                local_index, peer_index,
                common::Encode::HexEncode((*members[peer_index])->id).c_str());
        }
    }
}

void DkgCache::SetSwapKey(
        uint32_t network_id,
        uint32_t local_member_index,
        const std::string& id,
        uint32_t from_member_index,
        const std::string& secret_key_str) {
    prefix_db_->SaveSwapKey(
        network_id,
        local_member_index,
        id,
        from_member_index,
        secret_key_str);

    SwapKeyCacheKey key{elect_height, from_member_index, local_member_index, network_id};
    swap_keys_cache_[key] = secret_key_str;
}

bool DkgCache::GetSwapKey(
        uint32_t network_id,
        uint32_t local_member_index,
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
        bls::protobuf::VerifyVecBrdReq* verfy_req) {
    auto it = verify_g2_cache_.find(id);
    if (it != verify_g2_cache_.end()) {
        if (verfy_req != nullptr) {
            *verfy_req = it->second;
        }
        return true;
    }

    bls::protobuf::VerifyVecBrdReq tmp_verfy_req;
    if (!prefix_db_->GetBlsVerifyG2(id, &tmp_verfy_req)) {
        return false;
    }

    if (verfy_req != nullptr) {
        *verfy_req = tmp_verfy_req;
    }

    verify_g2_cache_[id] = tmp_verfy_req;
    return true;
}

void DkgCache::SetBlsVerifyG2(
        const std::string& id,
        const bls::protobuf::VerifyVecBrdReq& verfy_req) {
    prefix_db_->AddBlsVerifyG2(id, verfy_req);
    verify_g2_cache_[id] = verfy_req;
}

}  // namespace bls
}  // namespace shardora