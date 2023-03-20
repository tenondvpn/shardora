#pragma once

#include "security/ecdsa/crypto_utils.h"

namespace zjchain {

namespace security {

class Security {
public:
    virtual int SetPrivateKey(const std::string& prikey) = 0;
    virtual int Sign(const std::string& msg, std::string* sign) = 0;
    virtual int Verify(
        const std::string& msg,
        const std::string& pubkey,
        const std::string& sign) = 0;
    virtual std::string Recover(
        const std::string& sign,
        const std::string& hash) = 0;
    virtual const std::string& GetPrikey() const = 0;
    virtual const std::string& GetAddress() const = 0;
    virtual std::string GetAddress(const std::string& pubkey) = 0;
    virtual const std::string& GetPublicKey() const = 0;
    virtual const std::string& GetPublicKeyUnCompressed() const = 0;
    virtual int Encrypt(const std::string& msg, const std::string& key, std::string* out) = 0;
    virtual int Decrypt(const std::string& msg, const std::string& key, std::string* out) = 0;
    virtual int GetEcdhKey(const std::string& peer_pubkey, std::string* ecdh_key) = 0;
    virtual bool IsValidPublicKey(const std::string& pubkey) = 0;
    virtual std::string UnicastAddress(const std::string& src_address) = 0;
    virtual std::string GetSign(const std::string& r, const std::string& s, uint8_t v) = 0;

protected:
    Security() {}
    virtual ~Security() {}
};

}  // namespace security

}  // namespace zjchain
