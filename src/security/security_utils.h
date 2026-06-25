#pragma once

#include <memory>
#include <vector>

#include "common/hash.h"
#include "common/log.h"
#include "common/utils.h"

#define CRYPTO_DEBUG(fmt, ...) SHARDORA_DEBUG("[crypto]" fmt, ## __VA_ARGS__)
#define CRYPTO_INFO(fmt, ...) SHARDORA_DEBUG("[crypto]" fmt, ## __VA_ARGS__)
#define CRYPTO_WARN(fmt, ...) SHARDORA_WARN("[crypto]" fmt, ## __VA_ARGS__)
#define CRYPTO_ERROR(fmt, ...) SHARDORA_ERROR("[crypto]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace security {

enum SecurityErrorCode {
    kSecuritySuccess = 0,
    kSecurityError = 1,
};

using RawPrivateKey = std::pair<const char*, uint32_t>;

inline static std::string GetContractAddress(
        const std::string& from,
        const std::string& nonce) {
    // Ethereum CREATE address formula:
    //   contract_addr = keccak256(RLP([sender, nonce]))[-20:]
    //
    // 'from' is the 20-byte sender address.
    // 'nonce'  plays the role of nonce (arbitrary bytes, RLP-encoded as a byte string).
    //
    // RLP encoding helpers (inline, no external dependency):
    auto rlp_bytes = [](const std::string& b) -> std::string {
        if (b.empty()) return std::string(1, '\x80');
        if (b.size() == 1 && static_cast<uint8_t>(b[0]) < 0x80) return b;
        if (b.size() <= 55)
            return std::string(1, static_cast<char>(0x80 + b.size())) + b;
        // long string (>55 bytes)
        std::string len_be;
        size_t sz = b.size();
        while (sz > 0) { len_be.push_back(static_cast<char>(sz & 0xff)); sz >>= 8; }
        std::reverse(len_be.begin(), len_be.end());
        return std::string(1, static_cast<char>(0xb7 + len_be.size())) + len_be + b;
    };
    auto rlp_list = [](const std::string& payload) -> std::string {
        if (payload.size() <= 55)
            return std::string(1, static_cast<char>(0xc0 + payload.size())) + payload;
        std::string len_be;
        size_t sz = payload.size();
        while (sz > 0) { len_be.push_back(static_cast<char>(sz & 0xff)); sz >>= 8; }
        std::reverse(len_be.begin(), len_be.end());
        return std::string(1, static_cast<char>(0xf7 + len_be.size())) + len_be + payload;
    };

    // Normalise sender to exactly 20 bytes
    std::string sender;
    if (from.size() >= 20) {
        sender = from.substr(from.size() - 20, 20);
    } else {
        sender = std::string(20 - from.size(), '\0') + from;
    }

    std::string payload = rlp_bytes(sender) + rlp_bytes(nonce);
    std::string rlp = rlp_list(payload);
    std::string h = common::Hash::keccak256(rlp);
    return h.substr(12, 20);  // last 20 bytes
}

}  // namespace security

}  // namespace shardora
