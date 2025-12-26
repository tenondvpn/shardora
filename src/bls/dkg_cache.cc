#include "bls/dkg_cache.h"
#include "common/global_info.h"

namespace shardora {
namespace bls {

DkgCache::DkgCache(std::shared_ptr<protos::PrefixDb>& prefix_db)
        : prefix_db_(prefix_db) {}

DkgCache::~DkgCache() {}

void DkgCache::SetSwapKey(
        uint32_t network_id,
        uint32_t local_member_index,
        uint64_t elect_height,
        uint32_t from_member_index,
        const std::string& secret_key_str) {
    prefix_db_->SaveSwapKey(
        network_id,
        local_member_index,
        elect_height,
        local_member_index,
        from_member_index,
        secret_key_str);

    std::lock_guard<std::mutex> lock(swap_key_mutex_);
    SwapKeyCacheKey key{elect_height, from_member_index, local_member_index, network_id};
    swap_keys_cache_[key] = secret_key_str;
}

bool DkgCache::GetSwapKey(
        uint32_t network_id,
        uint32_t local_member_index,
        uint64_t elect_height,
        uint32_t from_member_index,
        std::string& secret_key_str) {
    SwapKeyCacheKey key{elect_height, from_member_index, local_member_index, network_id};
    // Try cache first
    {
        std::lock_guard<std::mutex> lock(swap_key_mutex_);
        auto it = swap_keys_cache_.find(key);
        if (it != swap_keys_cache_.end()) {
            secret_key_str = it->second;
            return true;
        }
    }

    // Fallback to DB
    if (!prefix_db_->GetSwapKey(
            network_id,
            local_member_index,
            elect_height,
            local_member_index,
            from_member_index,
            &secret_key_str)) {
        return false;
    }

    // Update cache
    std::lock_guard<std::mutex> lock(swap_key_mutex_);
    swap_keys_cache_[key] = secret_key_str;
    return true;
}

bool DkgCache::GetBlsVerifyG2(
        const std::string& id,
        bls::protobuf::VerifyVecBrdReq* verfy_req) {
    {
        std::lock_guard<std::mutex> lock(verify_g2_mutex_);
        auto it = verify_g2_cache_.find(id);
        if (it != verify_g2_cache_.end()) {
            *verfy_req = it->second;
            return true;
        }
    }

    if (!prefix_db_->GetBlsVerifyG2(id, verfy_req)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(verify_g2_mutex_);
    verify_g2_cache_[id] = *verfy_req;
    return true;
}

void DkgCache::SetBlsVerifyG2(
        const std::string& id,
        const bls::protobuf::VerifyVecBrdReq& verfy_req) {
    prefix_db_->AddBlsVerifyG2(id, verfy_req);
    std::lock_guard<std::mutex> lock(verify_g2_mutex_);
    verify_g2_cache_[id] = verfy_req;
}

void DkgCache::OnNewElection(uint64_t elect_height) {
    std::lock_guard<std::mutex> lock(swap_key_mutex_);
    elect_height_queue_.push_back(elect_height);
    if (elect_height_queue_.size() <= kMaxKeepElectionCount) {
        return;
    }

    uint64_t min_height = elect_height_queue_.front();
    elect_height_queue_.erase(elect_height_queue_.begin());

    for (auto it = swap_keys_cache_.begin(); it != swap_keys_cache_.end(); ) {
        if (it->first.elect_height < min_height) {
            it = swap_keys_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace bls
}  // namespace shardora