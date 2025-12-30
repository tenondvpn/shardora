#pragma once

#include "common/unique_map.h"

#include <oqs/oqs.h>

#include "security/security.h"

namespace shardora {

namespace security {


class Oqs : public Security {
public:
    Oqs() {
        sig_ptr_ = OQS_SIG_new(OQS_SIG_alg_dilithium_2);
    }

    virtual ~Oqs() {
        if (sig_ptr_ != nullptr) {
            OQS_SIG_free(sig_ptr_);
        }
    }

    virtual int SetPrivateKey(const std::string& prikey);
    int SetPrivateKey(const std::string& prikey, const std::string& pubkey);
    virtual int Sign(const std::string& hash, std::string* sign);
    virtual int Verify(const std::string& hash, const std::string& pubkey, const std::string& sign);
    virtual std::string Recover(
        const std::string& sign,
        const std::string& hash);

    virtual const std::string& GetPrikey() const {
        return str_prikey_;
    }

    virtual const std::string& GetAddress() const;
    virtual std::string GetAddress(const std::string& pubkey);
    virtual const std::string& GetPublicKey() const;
    virtual const std::string& GetPublicKeyUnCompressed() const;
    virtual int Encrypt(const std::string& msg, const std::string& key, std::string* out);
    virtual int Decrypt(const std::string& msg, const std::string& key, std::string* out);

    virtual int GetEcdhKey(const std::string& peer_pubkey, std::string* ecdh_key) {
        SHARDORA_FATAL("invalid!");
        return -1;
    }

    virtual bool IsValidPublicKey(const std::string& pubkey);
    virtual std::string UnicastAddress(const std::string& src_address);
    virtual std::string GetSign(const std::string& r, const std::string& s, uint8_t v);

private:
    OQS_SIG* sig_ptr_ = nullptr;
    uint8_t public_key_[OQS_SIG_dilithium_2_length_public_key];
    uint8_t secret_key_[OQS_SIG_dilithium_2_length_secret_key];
    std::string str_prikey_;
    std::string str_addr_;
    std::string str_pk_;

    DISALLOW_COPY_AND_ASSIGN(Oqs);
};

}  // namespace security

}  // namespace shardora
