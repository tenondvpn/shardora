#pragma once

#include "security/ecdsa/crypto_utils.h"
#include "security/ecdsa/private_key.h"
#include "security/ecdsa/public_key.h"
#include "security/ecdsa/curve.h"

namespace zjchain {

namespace security {

class EcdhCreateKey {
public:
    EcdhCreateKey();
    ~EcdhCreateKey();
    int Init(Curve* curve, PrivateKey* prikey, PublicKey* pubkey);
    int CreateKey(const PublicKey& peer_pubkey, std::string* sec_key);

private:
    Curve* curve_ptr_ = nullptr;
    PrivateKey* prikey_ptr_ = nullptr;
    PublicKey* pubkey_ptr_ = nullptr;
    EC_KEY *ec_key_{ nullptr };
    int field_size_{ 0 };

    DISALLOW_COPY_AND_ASSIGN(EcdhCreateKey);
};

}  // namespace security

}  // namespace zjchain
