#pragma once

#include <memory>
#include <vector>

#include "common/hash.h"
#include "common/log.h"
#include "common/utils.h"

#define CRYPTO_DEBUG(fmt, ...) ZJC_DEBUG("[crypto]" fmt, ## __VA_ARGS__)
#define CRYPTO_INFO(fmt, ...) ZJC_INFO("[crypto]" fmt, ## __VA_ARGS__)
#define CRYPTO_WARN(fmt, ...) ZJC_WARN("[crypto]" fmt, ## __VA_ARGS__)
#define CRYPTO_ERROR(fmt, ...) ZJC_ERROR("[crypto]" fmt, ## __VA_ARGS__)

namespace zjchain {

namespace security {

enum SecurityErrorCode {
    kSecuritySuccess = 0,
    kSecurityError = 1,
};

static const uint32_t kUnicastAddressLength = 20u;

inline static std::string GetContractAddress(
        const std::string& from,
        const std::string& gid,
        const std::string& code_hash) {
    return common::Hash::keccak256(from + gid + code_hash).substr(12, 20);
}

}  // namespace security

}  // namespace zjchain
