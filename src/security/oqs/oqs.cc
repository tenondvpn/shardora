#include "security/oqs/oqs.h"

#include <gmssl/sm2_recover.h>

#include "common/encode.h"
#include "common/log.h"
#include "common/time_utils.h"

namespace shardora {

namespace security {

int Oqs::SetPrivateKey(const std::string& prikey) {
    if (sig_ptr_ != nullptr) {
        OQS_SIG_free(sig_ptr_);
    }
    
    sig_ptr_ = OQS_SIG_new(OQS_SIG_alg_dilithium_2);
    auto rc = OQS_SIG_keypair(sig_ptr_, public_key_, secret_key_);
    if (rc != OQS_SUCCESS) {
        SHARDORA_ERROR("Keypair generation failed");
        return kSecurityError;
    }

    str_prikey_ = std::string((char*)secret_key_, sizeof(secret_key_));
    str_pk_ = std::string((char*)public_key_, sizeof(public_key_));
    str_addr_ = common::Hash::keccak256(str_pk_).substr(0, 20);
    std::cout << "prikey: " << common::Encode::HexEncode(str_prikey_) 
        << ", pubkey: " << common::Encode::HexEncode(str_pk_) 
        << ", address: " << common::Encode::HexEncode(str_addr_) 
        << std::endl;
    return kSecuritySuccess;
}

int Oqs::SetPrivateKey(const std::string& prikey, const std::string& pubkey) {
    if (sig_ptr_ != nullptr) {
        OQS_SIG_free(sig_ptr_);
    }

    sig_ptr_ = OQS_SIG_new(OQS_SIG_alg_dilithium_2);
    memcpy(secret_key_, prikey.c_str(), prikey.size());
    memcpy(public_key_, pubkey.c_str(), pubkey.size());
    str_pk_ = std::string((char*)public_key_, sizeof(public_key_));
    str_addr_ = common::Hash::keccak256(str_pk_).substr(0, 20);
    return kSecuritySuccess;
}

int Oqs::Sign(const std::string &hash, std::string *sign) {
    size_t sig_len;
    uint8_t signature[OQS_SIG_dilithium_2_length_signature];
    auto rc = OQS_SIG_sign(sig_ptr_, signature, &sig_len, (uint8_t*)hash.c_str(), hash.size(), secret_key_);
    if (rc != OQS_SUCCESS) {
        SHARDORA_ERROR("Signing failed");
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
        SHARDORA_ERROR("Signature verification failed!");
        return kSecurityError;
    }

    return kSecuritySuccess;
}

std::string Oqs::GetSign(const std::string& r, const std::string& s, uint8_t v) {
    SHARDORA_FATAL("invalid!");
    return "";
}

std::string Oqs::Recover(
        const std::string& sign,
        const std::string& hash) {
    SHARDORA_FATAL("invalid!");
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
    SHARDORA_FATAL("invalid!");
    return -1;
}

int Oqs::Decrypt(const std::string& msg, const std::string& key, std::string* out) {
    SHARDORA_FATAL("invalid!");
    return -1;
}

bool Oqs::IsValidPublicKey(const std::string& pubkey) {
    SHARDORA_FATAL("invalid!");
    return false;
}

std::string Oqs::UnicastAddress(const std::string& src_address) {
    SHARDORA_FATAL("invalid");
    return "";
}

}  // namespace security

}  // namespace shardora
