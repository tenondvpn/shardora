#pragma once

#include <memory>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <secp256k1/secp256k1.h>

#include "common/utils.h"
#include "security/ecdsa/curve.h"
#include "security/ecdsa/private_key.h"

namespace zjchain {

namespace security {

class PublicKey {
public:
    PublicKey(const Curve& curve);
    explicit PublicKey(const Curve& curve, PrivateKey& privkey);
    explicit PublicKey(const Curve& curve, const std::string& src);
    PublicKey(const PublicKey&);
    ~PublicKey();
    const std::shared_ptr<EC_POINT>& ec_point() const {
        return ec_point_;
    }

    PublicKey& operator=(const PublicKey& src);
    bool operator==(const PublicKey& r) const;
    uint32_t Serialize(std::string& dst, bool compress = true) const;
    int Deserialize(const std::string& src);
    int FromPrivateKey(const Curve& curve, PrivateKey& privkey);
    const secp256k1_pubkey* pubkey() const {
        return &pubkey_;
    }

    const std::string& str_pubkey() const {
        return str_pubkey_;
    }

private:
    int DeserializeToSecp256k1(const std::string& src);

    const Curve& curve_;
    std::shared_ptr<EC_POINT> ec_point_{ nullptr };
    secp256k1_pubkey pubkey_;
    std::string str_pubkey_;
};

}  // namespace security

}  // namespace zjchain
