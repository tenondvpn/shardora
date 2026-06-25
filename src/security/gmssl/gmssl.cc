#include "security/ecdsa/ecdsa.h"

#include <gmssl/sm2.h>
#include <cstring>
#include <stdexcept>

#include "common/encode.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "security/gmssl/gmssl.h"

namespace shardora {

namespace security {

int GmSsl::SetPrivateKey(const std::string& prikey) {
    //assert(prikey.size() == 32);
    str_prikey_ = prikey;
    prikey_ = std::make_shared<SM2_KEY>();
    // Generate public key from private key
    if (sm2_key_set_private_key(prikey_.get(), (uint8_t*)prikey.c_str()) != 1) {
        SHARDORA_ERROR("Failed to generate public key from private key.");
        return kSecurityError;
    }

    str_pk_ = std::string((char*)prikey_->public_key.x, 32) + std::string((char*)prikey_->public_key.y, 32);
    str_addr_ = common::Hash::sm3(str_pk_).substr(0, 20);
    return kSecuritySuccess;
}

int GmSsl::Sign(const std::string &hash, std::string *sign) {
	SM2_SIGNATURE sig;
	sm2_do_sign(prikey_.get(), (uint8_t*)hash.c_str(), &sig);
    *sign = std::string((char*)sig.r, 32) + std::string((char*)sig.s, 32);
    return kSecuritySuccess;
}

int GmSsl::Verify(const std::string& hash, const std::string& str_pk, const std::string& sign) {
    if (hash.size() != 32 || str_pk.size() != 64 || sign.size() != 64) {
        return kSecurityError;
    }

    SM2_SIGNATURE sig;
    memcpy(sig.r, sign.data(), sizeof(sig.r));
    memcpy(sig.s, sign.data() + sizeof(sig.r), sizeof(sig.s));

    SM2_POINT public_key;
    memcpy(public_key.x, str_pk.data(), sizeof(public_key.x));
    memcpy(public_key.y, str_pk.data() + sizeof(public_key.x), sizeof(public_key.y));

    SM2_KEY key;
    if (sm2_key_set_public_key(&key, &public_key) != 1 ||
            sm2_do_verify(&key, reinterpret_cast<const uint8_t*>(hash.data()), &sig) != 1) {
        return kSecurityError;
    }

    return kSecuritySuccess;
}

std::string GmSsl::GetSign(const std::string& r, const std::string& s, uint8_t v) {
    throw std::logic_error("GmSsl::GetSign not implemented");
}

std::string GmSsl::Recover(
        const std::string& sign,
        const std::string& hash) {
    throw std::logic_error("GmSsl::Recover not implemented");
}

const std::string& GmSsl::GetAddress() const {
    return str_addr_;
}

std::string GmSsl::GetAddress(const std::string& pubkey) {
    return common::Hash::sm3(pubkey).substr(0, 20);
}

const std::string& GmSsl::GetPublicKey() const {
    return str_pk_;
}

const std::string& GmSsl::GetPublicKeyUnCompressed() const {
    return str_pk_;
}

int GmSsl::Encrypt(const std::string& msg, RawPrivateKey key, std::string* out) {
    throw std::logic_error("GmSsl::Encrypt not implemented");
}

int GmSsl::Decrypt(const std::string& msg, RawPrivateKey key, std::string* out) {
    throw std::logic_error("GmSsl::Decrypt not implemented");
}

bool GmSsl::IsValidPublicKey(const std::string& pubkey) {
    throw std::logic_error("GmSsl::IsValidPublicKey not implemented");
}

std::string GmSsl::UnicastAddress(const std::string& src_address) {
    throw std::logic_error("GmSsl::UnicastAddress not implemented");
}

}  // namespace security

}  // namespace shardora
