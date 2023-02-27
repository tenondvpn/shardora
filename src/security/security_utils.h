#pragma once

#include <memory>
#include <vector>

#include "common/utils.h"
#include "common/log.h"

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

}  // namespace security

}  // namespace zjchain
