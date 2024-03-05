#pragma once

#include "common/unique_map.h"

#include "security/ecdsa/private_key.h"
#include "security/ecdsa/public_key.h"
#include "security/ecdsa/ecdh_create_key.h"
#include "security/ecdsa/curve.h"
#include "security/security.h"

namespace zjchain {

namespace security {


class Ecdsa : public Security {
public:
    Ecdsa() : pubkey_(curve_) {}

    virtual ~Ecdsa() {}

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
    virtual int GetEcdhKey(const std::string& peer_pubkey, std::string* ecdh_key);
    virtual bool IsValidPublicKey(const std::string& pubkey);
    virtual std::string UnicastAddress(const std::string& src_address);
    virtual std::string GetSign(const std::string& r, const std::string& s, uint8_t v);
    int GetEcdhKey(const PublicKey& peer_pubkey, std::string* ecdh_key);
    std::shared_ptr<PublicKey> GetPublicKey(const std::string& pk) {
        auto pk_ptr = std::make_shared<PublicKey>(curve_);
        if (pk_ptr->Deserialize(pk) != kSecuritySuccess) {
            return nullptr;
        }

        return pk_ptr;
    }

private:
    std::shared_ptr<PrivateKey> prikey_ = nullptr;
    Curve curve_;
    PublicKey pubkey_;
    EcdhCreateKey ecdh_key_;
    std::string str_prikey_;
    std::string str_addr_;
    std::string str_pk_;
//     common::UniqueMap<std::string, std::string, 16, 4> pk_addr_map_;

    DISALLOW_COPY_AND_ASSIGN(Ecdsa);
};

}  // namespace security

}  // namespace zjchain
