#pragma once

#include "security/security_utils.h"

namespace zjchain {

namespace security {

typedef std::vector<uint8_t> bytes;
static const uint32_t kPublicCompresssedSizeBytes = 33u;
static const uint32_t kCommitPointHashSize = 32u;
static const uint32_t kChallengeSize = 32u;
static const uint32_t kResponseSize = 32u;
static const uint8_t kSecondHashFunctionByte = 0x01;
static const uint8_t kThirdHashFunctionByte = 0x11;
static const uint32_t kCommitSecretSize = 32u;
static const uint32_t kCommitPointSize = 33u;
static const uint32_t kPrivateKeySize = 32u;
static const uint32_t kPublicCompressKeySize = 33u;
static const uint32_t kPublicKeyUncompressSize = 65u;
static const uint32_t kZjcAddressSize = 20u;
static const uint32_t kSignatureSize = 65u;

}  // namespace security

}  // namespace zjchain
