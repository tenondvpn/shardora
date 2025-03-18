#include "security/oqs/oqs.h"

#include <gmssl/sm2_recover.h>

#include "common/encode.h"
#include "common/log.h"
#include "common/time_utils.h"

namespace shardora {

namespace security {

int Oqs::SetPrivateKey(const std::string& prikey) {
    sig_ptr_ = OQS_SIG_new(OQS_SIG_alg_dilithium_2);
    auto rc = OQS_SIG_keypair(sig_ptr_, public_key_, secret_key_);
    if (rc != OQS_SUCCESS) {
        ZJC_ERROR("Keypair generation failed");
        return kSecurityError;
    }

    str_pk_ = std::string((char*)public_key_, sizeof(public_key_));
    str_addr_ = common::Hash::keccak256(str_pk_).substr(0, 20);
    return kSecuritySuccess;
}

int Oqs::Sign(const std::string &hash, std::string *sign) {
    size_t sig_len;
    uint8_t signature[OQS_SIG_dilithium_2_length_signature];
    auto rc = OQS_SIG_sign(sig_ptr_, signature, &sig_len, (uint8_t*)hash.c_str(), hash.size(), secret_key_);
    if (rc != OQS_SUCCESS) {
        ZJC_ERROR("Signing failed");
        return kSecurityError;
    }

    *sign = std::string((char*)signature, sizeof(signature));
    return kSecuritySuccess;
}

int Oqs::Verify(const std::string& hash, const std::string& str_pk, const std::string& sign) {
    auto sig_ptr = OQS_SIG_new(OQS_SIG_alg_dilithium_2);
    auto rc = OQS_SIG_verify(
        sig_ptr, 
        (uint8_t*)hash.c_str(), 
        hash.size(), 
        (uint8_t*)sign.c_str(), 
        sign.size(), 
        (uint8_t*)str_pk.c_str());
    if (rc != OQS_SUCCESS) {
        ZJC_ERROR("Signature verification failed!");
        return kSecurityError;
    }

    return kSecuritySuccess;
}

std::string Oqs::GetSign(const std::string& r, const std::string& s, uint8_t v) {
    ZJC_FATAL("invalid!");
    return "";
}

std::string Oqs::Recover(
        const std::string& sign,
        const std::string& hash) {
    ZJC_FATAL("invalid!");
    return "";
}

const std::string& Oqs::GetAddress() const {
    return str_addr_;
}

std::string Oqs::GetAddress(const std::string& pubkey) {
    return common::Hash::keccak256(pubkey).substr(0, 20);
}

const std::string& Oqs::GetPublicKey() const {
    return str_pk_;
}

const std::string& Oqs::GetPublicKeyUnCompressed() const {
    return str_pk_;
}

int Oqs::Encrypt(const std::string& msg, const std::string& key, std::string* out) {
    ZJC_FATAL("invalid!");
    return -1;
}

int Oqs::Decrypt(const std::string& msg, const std::string& key, std::string* out) {
    ZJC_FATAL("invalid!");
    return -1;
}

bool Oqs::IsValidPublicKey(const std::string& pubkey) {
    ZJC_FATAL("invalid!");
    return false;
}

std::string Oqs::UnicastAddress(const std::string& src_address) {
    ZJC_FATAL("invalid");
    return "";
}

}  // namespace security

}  // namespace shardora
