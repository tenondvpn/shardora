#pragma once

#include <stdexcept>

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
    virtual int SetPrivateKey(const char* prikey, uint32_t length) {
        throw std::logic_error("GmSsl::SetPrivateKey(char*, uint32_t) not implemented");
    }
    virtual int Sign(const std::string& hash, std::string* sign);
    virtual int Verify(const std::string& hash, const std::string& pubkey, const std::string& sign);
    virtual std::string Recover(
        const std::string& sign,
        const std::string& hash);

    virtual RawPrivateKey GetPrikey() const {
        return std::make_pair(str_prikey_.c_str(), str_prikey_.size());
    }

    virtual const std::string& GetAddress() const;
    virtual std::string GetAddress(const std::string& pubkey);
    virtual const std::string& GetPublicKey() const;
    virtual const std::string& GetPublicKeyUnCompressed() const;
    virtual int Encrypt(const std::string& msg, RawPrivateKey key, std::string* out);
    virtual int Decrypt(const std::string& msg, RawPrivateKey key, std::string* out);

    virtual int GetEcdhKey(const std::string& peer_pubkey, std::string* ecdh_key) {
        throw std::logic_error("GmSsl::GetEcdhKey not implemented");
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
