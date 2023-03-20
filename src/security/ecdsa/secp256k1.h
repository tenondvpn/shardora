#pragma once

#include <memory>

#include <secp256k1/secp256k1.h>
#include <secp256k1/secp256k1_ecdh.h>
#include <secp256k1/secp256k1_recovery.h>

#include "common/utils.h"
#include "security/ecdsa/public_key.h"
#include "security/ecdsa/crypto_utils.h"

namespace zjchain {

namespace security {

// static std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> s_ctx{
//     secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY),
//     &secp256k1_context_destroy
//     };

class Secp256k1 {
public:
    static Secp256k1* Instance();
    secp256k1_context const* getCtx() {
        return ctx_;
    }

    bool Secp256k1Sign(
        const std::string& msg,
        const PrivateKey& privkey,
        std::string* sign);
    bool Secp256k1Verify(
        const std::string& msg,
        const PublicKey& pubkey,
        const std::string& sign);
    bool Secp256k1Verify(
        const std::string& msg,
        const std::string& pubkey,
        const std::string& sign);
    std::string GetSign(const std::string& r, const std::string& s, uint8_t v);
    std::string Recover(const std::string& sign, const std::string& hash, bool compressed);
    std::string RecoverForContract(const std::string& sign, const std::string& hash);
    std::string sha3(const std::string & input);
    std::string ToPublicFromCompressed(const std::string& in_pubkey);
    std::string ToAddressWithPublicKey(const Curve& curve, const std::string& pub_key);
    std::string UnicastAddress(const std::string& src_address) {
        assert(src_address.size() >= kUnicastAddressLength);
        return src_address.substr(
            src_address.size() - kUnicastAddressLength,
            kUnicastAddressLength);
    }

private:
    Secp256k1();
    ~Secp256k1();
    secp256k1_context* ctx_ = nullptr;
    DISALLOW_COPY_AND_ASSIGN(Secp256k1);

};

}  // namespace security

}  // namespace zjchain
