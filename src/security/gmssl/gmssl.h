#pragma once

#include "common/unique_map.h"

#include <gmssl/sm2.h>
#include <gmssl/error.h>
#include <gmssl/rand.h>
#include <gmssl/sm4.h>
#include <gmssl/sm3.h>

#include "security/security.h"

namespace shardora {

namespace security {


class GmSsl : public Security {
public:
    GmSsl() {}

    virtual ~GmSsl() {}

    virtual int SetPrivateKey(const std::string& prikey);
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
    std::shared_ptr<SM2_KEY> prikey_ = nullptr;
    std::string str_prikey_;
    std::string str_addr_;
    std::string str_pk_;

    DISALLOW_COPY_AND_ASSIGN(GmSsl);
};

}  // namespace security

}  // namespace shardora
