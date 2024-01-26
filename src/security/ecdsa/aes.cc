#include "security/ecdsa/aes.h"

#include <cassert>
#include <openssl/aes.h>
#include <string>

#include "common/encode.h"
#include "security/ecdsa/crypto_utils.h"

namespace zjchain {

namespace security {

int Aes::Encrypt(char* str_in, int len, char* key, int key_len, char* out) {
    assert(key_len == 16 || key_len == 24 || key_len == 32);
    if (!str_in || !key || !out || len <= 0 || key_len <= 0) {
        return kSecurityError;
    }

    unsigned char iv[AES_BLOCK_SIZE];
    std::fill(iv, iv + AES_BLOCK_SIZE, 0);
    AES_KEY aes;
    if (AES_set_encrypt_key((unsigned char*)key, key_len * 8, &aes) < 0) {
        return kSecurityError;
    }

    AES_cbc_encrypt((unsigned char*)str_in, (unsigned char*)out, len, &aes, iv, AES_ENCRYPT);
    return kSecuritySuccess;
}

int Aes::Decrypt(char* str_in, int len, char* key, int key_len, char* out) {
    assert(key_len == 16 || key_len == 24 || key_len == 32);
    if (!str_in || !key || !out || len <= 0 || key_len <= 0) {
        return kSecurityError;
    }

    unsigned char iv[AES_BLOCK_SIZE];
    std::fill(iv, iv + AES_BLOCK_SIZE, 0);

    AES_KEY aes;
    if (AES_set_decrypt_key((unsigned char*)key, key_len * 8, &aes) < 0) {
        return kSecurityError;
    }

    AES_cbc_encrypt((unsigned char*)str_in, (unsigned char*)out, len, &aes, iv, AES_DECRYPT);
    return kSecuritySuccess;
}

int Aes::CfbEncrypt(char* str_in, int len, char* key, int key_len, char* out) {
    assert(key_len == 16 || key_len == 24 || key_len == 32);
    if (!str_in || !key || !out || len <= 0 || key_len <= 0) {
        return kSecurityError;
    }

    unsigned char iv[AES_BLOCK_SIZE];
    std::fill(iv, iv + AES_BLOCK_SIZE, 0);
    AES_KEY aes;
    if (AES_set_encrypt_key((unsigned char*)key, 256, &aes) < 0) {
        return kSecurityError;
    }
    int num = 0;
    AES_cfb128_encrypt((unsigned char*)str_in, (unsigned char*)out, len, &aes, iv, &num, AES_ENCRYPT);
    return kSecuritySuccess;
}

int Aes::CfbDecrypt(char* str_in, int len, char* key, int key_len, char* out) {
    assert(key_len == 16 || key_len == 24 || key_len == 32);
    if (!str_in || !key || !out || len <= 0 || key_len <= 0) {
        return kSecurityError;
    }

    unsigned char iv[AES_BLOCK_SIZE];
    std::fill(iv, iv + AES_BLOCK_SIZE, 0);
    AES_KEY aes;
    if (AES_set_decrypt_key((unsigned char*)key, 256, &aes) < 0) {
        return kSecurityError;
    }
    int num = 0;
    AES_cfb128_encrypt((unsigned char*)str_in, (unsigned char*)out, len, &aes, iv, &num, AES_DECRYPT);
    return kSecuritySuccess;
}

}  // namespace security

}  // namespace zjchain
