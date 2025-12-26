#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <unordered_map>

#include <libff/algebra/curves/alt_bn128/alt_bn128_fr.hpp>

#include "common/utils.h"
#include "protos/prefix_db.h"
#include "protos/bls.pb.h"

namespace shardora {
namespace bls {

class DkgCache {
public:
    DkgCache(std::shared_ptr<protos::PrefixDb>& prefix_db);
    ~DkgCache();

    bool GetSwapKey(
        uint32_t network_id,
        uint32_t local_member_index,
        uint64_t elect_height,
        uint32_t from_member_index,
        std::string& secret_key_str);

    void SetSwapKey(
        uint32_t network_id,
        uint32_t local_member_index,
        uint64_t elect_height,
        uint32_t from_member_index,
        const std::string& secret_key_str);

    bool GetBlsVerifyG2(
        const std::string& id,
        bls::protobuf::VerifyVecBrdReq* verfy_req);

    void SetBlsVerifyG2(
        const std::string& id,
        const bls::protobuf::VerifyVecBrdReq& verfy_req);

    void OnNewElection(uint64_t elect_height);

private:
    struct SwapKeyCacheKey {
        uint64_t elect_height;
        uint32_t from_member_index;
        uint32_t local_member_index;
        uint32_t network_id;

        bool operator==(const SwapKeyCacheKey& other) const {
            return elect_height == other.elect_height &&
                   from_member_index == other.from_member_index &&
                   local_member_index == other.local_member_index &&
                   network_id == other.network_id;
        }
    };

    struct SwapKeyCacheKeyHash {
        std::size_t operator()(const SwapKeyCacheKey& k) const {
            return std::hash<uint64_t>()(k.elect_height) ^
                   (std::hash<uint32_t>()(k.from_member_index) << 1) ^
                   (std::hash<uint32_t>()(k.local_member_index) << 2) ^
                   (std::hash<uint32_t>()(k.network_id) << 3);
        }
    };

    using SwapKeyCache = std::unordered_map<SwapKeyCacheKey, std::string, SwapKeyCacheKeyHash>;
    SwapKeyCache swap_keys_cache_;
    std::unordered_map<std::string, bls::protobuf::VerifyVecBrdReq> verify_g2_cache_;
    std::mutex swap_key_mutex_;
    std::mutex verify_g2_mutex_;

    std::shared_ptr<protos::PrefixDb> prefix_db_;
    std::vector<uint64_t> elect_height_queue_;
    static const uint32_t kMaxKeepElectionCount = 4;

    DISALLOW_COPY_AND_ASSIGN(DkgCache);
};

}  // namespace bls
}  // namespace shardora