#include "security/ecdsa/private_key.h"

#include <cassert>

#include "common//random.h"
#include "security/ecdsa/crypto_utils.h"
#include "security/ecdsa/security_string_trans.h"

namespace zjchain {

namespace security {

PrivateKey::PrivateKey(const std::string& src)
        : bignum_(BN_new(), BN_clear_free) {
    assert(bignum_ != nullptr);
    bignum_ = SecurityStringTrans::Instance()->StringToBignum(src);
    private_key_ = src;
}

PrivateKey::PrivateKey(const PrivateKey& src) : bignum_(BN_new(), BN_clear_free) {
    assert(bignum_ != nullptr);
    if (BN_copy(bignum_.get(), src.bignum_.get()) == NULL) {
        CRYPTO_ERROR("copy big num failed!");
        assert(false);
    }

    Serialize(private_key_);
}

PrivateKey& PrivateKey::operator=(const PrivateKey& src) {
    if (this == &src) {
        return *this;
    }

    if (BN_copy(bignum_.get(), src.bignum_.get()) == NULL) {
        CRYPTO_ERROR("copy big num failed!");
        assert(false);
    }

    private_key_ = src.private_key_;
    return *this;
}

bool PrivateKey::operator==(const PrivateKey& r) const {
    return private_key_ == r.private_key_;
}

uint32_t PrivateKey::Serialize(std::string& dst) const {
    SecurityStringTrans::Instance()->BignumToString(bignum_, dst);
    return kPrivateKeySize;
}

int PrivateKey::Deserialize(const std::string& src) {
    auto result = SecurityStringTrans::Instance()->StringToBignum(src);
    if (result == nullptr) {
        CRYPTO_ERROR("BIGNUMSerialize::GetNumber failed");
        return kSecurityError;
    }

    if (BN_copy(bignum_.get(), result.get()) == NULL) {
        CRYPTO_ERROR("PrivKey copy failed");
        return kSecurityError;
    }
    return kSecuritySuccess;
}

}  // namespace security

}  // namespace zjchain
