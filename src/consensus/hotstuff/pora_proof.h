#pragma once

#include <string>
#include <cstdint>

#include "common/hash.h"
#include "common/log.h"
#include "protos/prefix_db.h"

namespace shardora {
namespace hotstuff {

// Number of bytes sampled per PoRA challenge.
static constexpr size_t kPoraKappa = 1024;

// Derive a uint64 from the first 8 bytes of a hash string.
inline uint64_t PoraHashToU64(const std::string& h) {
    uint64_t v = 0;
    const size_t n = std::min(h.size(), size_t(8));
    for (size_t i = 0; i < n; ++i) {
        v = (v << 8) | static_cast<uint8_t>(h[i]);
    }
    return v;
}

// Compute PoRA proof for the current pool.
//
// Uses the parent block's BLS aggregate signature (sign_x, sign_y) as an
// unpredictable random seed, selects a committed historical block, reads
// kPoraKappa bytes at a random offset, and returns
//   SHA256(sampled_bytes || sign_x || sign_y)
//
// Returns empty when no history is available yet (genesis / early blocks).
inline std::string ComputePoraProof(
        const std::string& sign_x,
        const std::string& sign_y,
        uint32_t sharding_id,
        uint32_t pool_index,
        uint64_t h_max,
        protos::PrefixDb* prefix_db) {
    if (sign_x.empty() || sign_y.empty() || prefix_db == nullptr) {
        return {};
    }

    const std::string seed = sign_x + sign_y;
    // Target block height: 1 .. h_max (inclusive).
    const std::string h_seed = common::Hash::Sha256(seed);
    const uint64_t h_tgt = (PoraHashToU64(h_seed) % h_max) + 1;
    // Random offset (GetBlockSubValue clamps for us).
    const std::string off_seed = common::Hash::Sha256(seed + "off");
    const size_t offset = static_cast<size_t>(PoraHashToU64(off_seed));

    std::string sampled;
    if (!prefix_db->GetBlockSubValue(
            sharding_id, pool_index, h_tgt, offset, kPoraKappa, &sampled)) {
        SHARDORA_WARN("PoRA: GetBlockSubValue failed shard=%u pool=%u h=%lu",
            sharding_id, pool_index, h_tgt);
        return {};
    }

    if (sampled.empty()) {
        return {};
    }

    return common::Hash::Sha256(sampled + seed);
}

}  // namespace hotstuff
}  // namespace shardora
