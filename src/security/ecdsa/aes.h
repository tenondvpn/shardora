#pragma once

#include "openssl/aes.h"
#include "security/ecdsa/crypto_utils.h"

namespace zjchain {

namespace security {

class Aes {
public:
    static int Encrypt(char* str_in, int len, char* key, int key_len, char* out);
    static int Decrypt(char* str_in, int len, char* key, int key_len, char* out);
    static int CfbEncrypt(char* str_in, int len, char* key, int key_len, char* out);
    static int CfbDecrypt(char* str_in, int len, char* key, int key_len, char* out);

private:
    Aes();
    ~Aes();
    DISALLOW_COPY_AND_ASSIGN(Aes);
};

}  // namespace security

}  // namespace zjchain
