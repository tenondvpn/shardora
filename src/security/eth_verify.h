#pragma once

#include <string>
#include <algorithm>

#include "common/hash.h"
#include "common/encode.h"
#include "common/log.h"
#include "consensus/hotstuff/hotstuff_utils.h"
#include "security/ecdsa/secp256k1.h"
#include "security/ecdsa/crypto_utils.h"

namespace shardora {
namespace security {

// Verify an ETH-format transaction signature by rebuilding the EIP-155
// signing hash from eth_raw_tx and recovering the public key.
// Returns true if the recovered pubkey matches the pubkey stored in the tx.
inline bool VerifyEthSignature(
        const std::string& eth_raw_tx,
        const std::string& expected_pubkey,
        const std::string& sign) {
    if (eth_raw_tx.empty() || expected_pubkey.empty() || sign.size() != 65) {
        return false;
    }

    // ── 1. Decode the signed RLP to extract fields ──────────────────────
    const uint8_t* p = reinterpret_cast<const uint8_t*>(eth_raw_tx.data());
    size_t len = eth_raw_tx.size();
    if (len < 1 || p[0] < 0xc0) return false;

    // Parse outer list header
    size_t list_len = 0, hdr = 0;
    if (p[0] >= 0xf8) {
        hdr = 1 + (p[0] - 0xf7);
        if (hdr > len) return false;
        for (size_t i = 1; i < hdr; ++i) list_len = (list_len << 8) | p[i];
    } else {
        hdr = 1;
        list_len = p[0] - 0xc0;
    }
    p += hdr; len -= hdr;
    if (len < list_len) return false;

    // RLP item decoder
    auto decode_item = [](const uint8_t*& pp, size_t& ll, std::string& out) -> bool {
        if (ll < 1) return false;
        if (pp[0] <= 0x7f) {
            out = std::string(1, (char)pp[0]);
            pp++; ll--;
        } else if (pp[0] <= 0xb7) {
            size_t item_len = pp[0] - 0x80;
            if (ll < 1 + item_len) return false;
            out = std::string((char*)pp + 1, item_len);
            pp += 1 + item_len; ll -= 1 + item_len;
        } else if (pp[0] <= 0xbf) {
            size_t hlen = pp[0] - 0xb7;
            if (ll < 1 + hlen) return false;
            size_t item_len = 0;
            for (size_t i = 1; i <= hlen; ++i) item_len = (item_len << 8) | pp[i];
            if (ll < 1 + hlen + item_len) return false;
            out = std::string((char*)pp + 1 + hlen, item_len);
            pp += 1 + hlen + item_len; ll -= 1 + hlen + item_len;
        } else {
            return false;
        }
        return true;
    };

    // Decode 9 fields: nonce, gasPrice, gasLimit, to, value, data, v, r, s
    std::string s_nonce, s_gasprice, s_gaslimit, s_to, s_value, s_data, s_v, s_r, s_s;
    if (!decode_item(p, len, s_nonce))    return false;
    if (!decode_item(p, len, s_gasprice)) return false;
    if (!decode_item(p, len, s_gaslimit)) return false;
    if (!decode_item(p, len, s_to))       return false;
    if (!decode_item(p, len, s_value))    return false;
    if (!decode_item(p, len, s_data))     return false;
    if (!decode_item(p, len, s_v))        return false;
    if (!decode_item(p, len, s_r))        return false;
    if (!decode_item(p, len, s_s))        return false;

    // Extract v → recovery_id
    auto be_to_u64 = [](const std::string& s) -> uint64_t {
        uint64_t v = 0;
        for (unsigned char c : s) v = (v << 8) | c;
        return v;
    };
    uint64_t v_val = be_to_u64(s_v);
    uint8_t v_byte = (v_val >= 35) ? static_cast<uint8_t>((v_val - 35) % 2) : static_cast<uint8_t>(v_val);

    // ── 2. Rebuild EIP-155 signing hash ─────────────────────────────────
    // RLP([nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0])
    uint64_t chain_id = hotstuff::kGlobalChainId;

    auto rlp_encode_uint = [](uint64_t v) -> std::string {
        if (v == 0) return std::string(1, '\x80');
        std::string be;
        while (v > 0) { be.push_back(static_cast<char>(v & 0xff)); v >>= 8; }
        std::reverse(be.begin(), be.end());
        if (be.size() == 1 && static_cast<uint8_t>(be[0]) < 0x80) return be;
        return std::string(1, static_cast<char>(0x80 + be.size())) + be;
    };
    auto rlp_encode_bytes = [](const std::string& b) -> std::string {
        if (b.empty()) return std::string(1, '\x80');
        if (b.size() == 1 && static_cast<uint8_t>(b[0]) < 0x80) return b;
        if (b.size() <= 55)
            return std::string(1, static_cast<char>(0x80 + b.size())) + b;
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

    std::string payload;
    payload += rlp_encode_bytes(s_nonce.empty() ? std::string(1, '\0') : s_nonce);
    // For nonce/gasPrice/gasLimit/value: re-encode from the raw decoded bytes
    // But the original fields are already minimal big-endian, so we re-encode them as bytes.
    // Actually we need to re-encode them as RLP integers, which means using the raw bytes directly.
    // The simplest correct approach: re-encode from the original raw bytes.
    payload.clear();
    payload += rlp_encode_bytes(s_nonce);
    payload += rlp_encode_bytes(s_gasprice);
    payload += rlp_encode_bytes(s_gaslimit);
    payload += rlp_encode_bytes(s_to);
    payload += rlp_encode_bytes(s_value);
    payload += rlp_encode_bytes(s_data);
    payload += rlp_encode_uint(chain_id);
    payload += rlp_encode_uint(0);
    payload += rlp_encode_uint(0);
    std::string signing_rlp = rlp_list(payload);
    std::string signing_hash = common::Hash::keccak256(signing_rlp);

    // ── 3. Build signature and recover pubkey ───────────────────────────
    // r and s: left-pad to 32 bytes
    std::string r_32 = std::string(32 - std::min<size_t>(s_r.size(), 32), '\0') +
        s_r.substr(s_r.size() > 32 ? s_r.size() - 32 : 0);
    std::string s_32 = std::string(32 - std::min<size_t>(s_s.size(), 32), '\0') +
        s_s.substr(s_s.size() > 32 ? s_s.size() - 32 : 0);

    std::string sig_for_recover;
    sig_for_recover.reserve(65);
    sig_for_recover.append(r_32);
    sig_for_recover.append(s_32);
    sig_for_recover.push_back(static_cast<char>(v_byte));

    // Recover uncompressed pubkey (64 bytes, no 0x04 prefix)
    std::string recovered = Secp256k1::Instance()->Recover(sig_for_recover, signing_hash, false);
    if (recovered.empty()) {
        SHARDORA_WARN("VerifyEthSignature: pubkey recovery failed");
        return false;
    }

    // ── 4. Compare recovered pubkey with expected ───────────────────────
    // expected_pubkey may be 65 bytes (0x04 + X + Y) or 64 bytes (X + Y)
    std::string expected_raw = expected_pubkey;
    if (expected_raw.size() == kPublicKeyUncompressSize && expected_raw[0] == '\x04') {
        expected_raw = expected_raw.substr(1);  // strip 0x04 prefix
    }

    if (recovered.size() != expected_raw.size() || recovered != expected_raw) {
        SHARDORA_WARN("VerifyEthSignature: pubkey mismatch, recovered=%s, expected=%s",
            common::Encode::HexEncode(recovered).c_str(),
            common::Encode::HexEncode(expected_raw).c_str());
        return false;
    }

    return true;
}

}  // namespace security
}  // namespace shardora
