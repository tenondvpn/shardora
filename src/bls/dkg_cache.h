#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <unordered_map>
#include "bls/bls_utils.h"

#include "common/utils.h"
#include "protos/prefix_db.h"
#include "protos/bls.pb.h"

namespace shardora {
namespace bls {

class DkgCache {
public:
    DkgCache(std::shared_ptr<protos::PrefixDb>& prefix_db);
    ~DkgCache();
    void Init(uint32_t local_index, common::Members& members, uint32_t network_id);
    bool GetSwapKey(
        uint32_t network_id,
        uint32_t local_member_index,
        const std::string& id,
        uint32_t from_member_index,
        std::string* secret_key_str);
    bool GetBlsVerifyG2(
        const std::string& id,
        bls::protobuf::VerifyVecBrdReq* verfy_req);
    void SetSwapKey(
        uint32_t network_id,
        uint32_t local_member_index,
        const std::string& id,
        uint32_t from_member_index,
        const std::string& secret_key_str);
    const std::unordered_map<std::string, libff::alt_bn128_G2>& verify_g2_cache() const {
        return verify_g2_cache_;
    }

private:
    struct SwapKeyCacheKey {
        std::string id;
        uint32_t from_member_index;
        uint32_t local_member_index;
        uint32_t network_id;

        bool operator==(const SwapKeyCacheKey& other) const {
            return id == other.id &&
                   from_member_index == other.from_member_index &&
                   local_member_index == other.local_member_index &&
                   network_id == other.network_id;
        }
    };

    struct SwapKeyCacheKeyHash {
        std::size_t operator()(const SwapKeyCacheKey& k) const {
            return std::hash<std::string>()(k.id) ^
                   (std::hash<uint32_t>()(k.from_member_index) << 1) ^
                   (std::hash<uint32_t>()(k.local_member_index) << 2) ^
                   (std::hash<uint32_t>()(k.network_id) << 3);
        }
    };

    using SwapKeyCache = std::unordered_map<SwapKeyCacheKey, std::string, SwapKeyCacheKeyHash>;
    SwapKeyCache swap_keys_cache_;
    std::unordered_map<std::string, libff::alt_bn128_G2> verify_g2_cache_;
    std::shared_ptr<protos::PrefixDb> prefix_db_;

    DISALLOW_COPY_AND_ASSIGN(DkgCache);
};

}  // namespace bls
}  // namespace shardora