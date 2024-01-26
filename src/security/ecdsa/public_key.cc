#include "security/ecdsa/public_key.h"

#include "common/encode.h"
#include "security/ecdsa/crypto_utils.h"
#include "security/ecdsa/security_string_trans.h"
#include "security/ecdsa/secp256k1.h"

namespace zjchain {

namespace security {

PublicKey::PublicKey(const Curve& curve)
        : curve_(curve), ec_point_(
            EC_POINT_new(curve.group_.get()),
            EC_POINT_clear_free) {
    assert(ec_point_ != nullptr);
}

PublicKey::PublicKey(const Curve& curve, PrivateKey& privkey)
        : curve_(curve), ec_point_(
            EC_POINT_new(curve.group_.get()),
            EC_POINT_clear_free) {
    FromPrivateKey(curve, privkey);
}

PublicKey::PublicKey(const Curve& curve, const std::string& src) : curve_(curve) {
    assert(src.size() == kPublicKeyUncompressSize || src.size() == kPublicCompressKeySize);
    ec_point_ = SecurityStringTrans::Instance()->StringToEcPoint(curve, src);
    assert(ec_point_ != nullptr);
    Serialize(str_pubkey_);
    Serialize(str_pubkey_uncompressed_, false);
    DeserializeToSecp256k1(str_pubkey_);
}

PublicKey::PublicKey(const PublicKey& src)
        : curve_(src.curve_), ec_point_(
            EC_POINT_new(curve_.group_.get()),
            EC_POINT_clear_free) {
    assert(ec_point_ != nullptr);
    if (EC_POINT_copy(ec_point_.get(), src.ec_point_.get()) != 1) {
        CRYPTO_ERROR("copy ec point failed!");
        assert(false);
    }

    str_pubkey_ = src.str_pubkey_;
    str_pubkey_uncompressed_ = src.str_pubkey_uncompressed_;
    pubkey_ = src.pubkey_;
}

PublicKey::~PublicKey() {}

PublicKey& PublicKey::operator=(const PublicKey& src) {
    if (this == &src) {
        return *this;
    }

    if (EC_POINT_copy(ec_point_.get(), src.ec_point_.get()) != 1) {
        CRYPTO_ERROR("PubKey copy failed");
        assert(false);
    }

    str_pubkey_ = src.str_pubkey_;
    str_pubkey_uncompressed_ = src.str_pubkey_uncompressed_;
    pubkey_ = src.pubkey_;
    return *this;
}

bool PublicKey::operator==(const PublicKey& r) const {
    return str_pubkey_ == r.str_pubkey_;
}

uint32_t PublicKey::Serialize(std::string& dst, bool compress) const {
    SecurityStringTrans::Instance()->EcPointToString(curve_, ec_point_, compress, dst);
    return compress ? kPublicCompressKeySize: kPublicKeyUncompressSize;
}

int PublicKey::Deserialize(const std::string& src) {
    auto result = SecurityStringTrans::Instance()->StringToEcPoint(curve_, src);
    if (result == nullptr) {
        CRYPTO_ERROR("ECPOINTSerialize::GetNumber failed[%s]",
                common::Encode::HexEncode(src).c_str());
        return -1;
    }

    if (!EC_POINT_copy(ec_point_.get(), result.get())) {
        CRYPTO_ERROR("PubKey copy failed");
        return -1;
    }

    DeserializeToSecp256k1(src);
    return 0;
}

int PublicKey::FromPrivateKey(const Curve& curve, PrivateKey& privkey) {
    assert(ec_point_ != nullptr);
    if (BN_is_zero(privkey.bignum().get()) ||
        (BN_cmp(privkey.bignum().get(), curve.order_.get()) != -1)) {
        CRYPTO_ERROR("Input private key is invalid. Public key "
            "generation failed");
        return kSecurityError;
    }

    if (EC_POINT_mul(
            curve.group_.get(),
            ec_point_.get(),
            privkey.bignum().get(),
            NULL,
            NULL,
            NULL) == 0) {
        CRYPTO_ERROR("Public key generation failed");
        return kSecurityError;
    }

    Serialize(str_pubkey_);
    Serialize(str_pubkey_uncompressed_, false);
    DeserializeToSecp256k1(str_pubkey_);
    return kSecuritySuccess;
}

int PublicKey::DeserializeToSecp256k1(const std::string& src) {
    if (secp256k1_ec_pubkey_parse(
            Secp256k1::Instance()->getCtx(),
            &pubkey_,
            (uint8_t*)src.c_str(),
            src.size()) != 1) {
        return 1;
    }

    return 0;
}

}  // namespace security

}  // namespace zjchain
