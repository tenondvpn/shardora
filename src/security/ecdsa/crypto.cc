#include "security/ecdsa/crypto.h"

#include "common/encode.h"
#include "security/ecdsa/ecdh_create_key.h"
#include "security/ecdsa/aes.h"

namespace zjchain {

namespace security {

Crypto* Crypto::Instance() {
    static Crypto ins;
    return &ins;
}

int Crypto::GetEncryptData(
        const std::string& enc_key,
        const std::string& message,
        std::string* enc_data) {
    uint32_t data_size = (message.size() / AES_BLOCK_SIZE) * AES_BLOCK_SIZE + AES_BLOCK_SIZE;
    enc_data->resize(data_size, 0);
    if (security::Aes::Encrypt(
            (char*)message.c_str(),
            message.size(),
            (char*)enc_key.c_str(),
            enc_key.size(),
            (char*)&(*enc_data)[0]) != security::kSecuritySuccess) {
        return kSecurityError;
    }

    return kSecuritySuccess;
}

int Crypto::GetDecryptData(
        const std::string& enc_key,
        const std::string& crypt_message,
        std::string* dec_data) {
    dec_data->resize(crypt_message.size(), 0);
    if (security::Aes::Decrypt(
            (char*)crypt_message.c_str(),
            crypt_message.size(),
            (char*)enc_key.c_str(),
            enc_key.size(),
            (char*)&(*dec_data)[0]) != security::kSecuritySuccess) {
            CRYPTO_ERROR("Decrypt error!");
        return kSecurityError;
    }

    return kSecuritySuccess;
}



};  // namespace security

};  // namespace zjchain