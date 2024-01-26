#include "security/ecdsa/ecdh_create_key.h"

#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/evp.h>

namespace zjchain {

namespace security {

int EcdhCreateKey::Init(Curve* curve, PrivateKey* prikey_ptr, PublicKey* pubkey_ptr) {
    ec_key_ = EC_KEY_new();
    if (ec_key_ == NULL) {
        CRYPTO_ERROR("create ec ec_key_ failed!");
        return kSecurityError;
    }

    if (EC_KEY_set_group(ec_key_, curve->group_.get()) != 1) {
        CRYPTO_ERROR("ec_key_ set group failed!");
        return kSecurityError;
    }

    auto prikey = prikey_ptr->bignum().get();
    if (EC_KEY_set_private_key(ec_key_, prikey) != 1) {
        CRYPTO_ERROR("ec_key_ set private ec_key_ failed!");
        return kSecurityError;
    }

    auto pubkey = pubkey_ptr->ec_point().get();
    if (EC_KEY_set_public_key(ec_key_, pubkey) != 1) {
        CRYPTO_ERROR("ec_key_ set public ec_key_ failed!");
        return kSecurityError;
    }
    field_size_ = EC_GROUP_get_degree(EC_KEY_get0_group(ec_key_));
    auto secret_len = (field_size_ + 7) / 8;
    if (secret_len != 32) {
        CRYPTO_ERROR("secret_len error: %d!", secret_len);
        return kSecurityError;
    }
    return kSecuritySuccess;
}

int EcdhCreateKey::CreateKey(const PublicKey& peer_pubkey, std::string* sec_key) {
    auto secret_len = (field_size_ + 7) / 8;
    sec_key->resize(secret_len, 0);
    if (secret_len != 32 || (int32_t)sec_key->size() != secret_len) {
        CRYPTO_ERROR("secret_len error: %d, str size: %d!", secret_len, sec_key->size());
        return kSecurityError;
    }

    secret_len = ECDH_compute_key(
            ((void*)&((*sec_key)[0])),
            secret_len,
            peer_pubkey.ec_point().get(),
            ec_key_,
            NULL);
    if (secret_len <= 0) {
        CRYPTO_ERROR("ECDH_compute_key failed!");
        return kSecurityError;
    }
    return kSecuritySuccess;
}

EcdhCreateKey::EcdhCreateKey() {}

EcdhCreateKey::~EcdhCreateKey() {
    if (ec_key_ != nullptr) {
        EC_KEY_free(ec_key_);
    }
}


}  // namespace security

}  // namespace zjchain
