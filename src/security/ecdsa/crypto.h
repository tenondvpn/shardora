#pragma once

#include "common/utils.h"
#include "security/ecdsa/public_key.h"

namespace zjchain {

namespace security {

class Crypto {
public:
    static Crypto* Instance();
    int GetEncryptData(
        const std::string& enc_key,
        const std::string& message,
        std::string* enc_data);
    int GetDecryptData(
        const std::string& enc_key,
        const std::string& crypt_message,
        std::string* dec_data);

private:
    Crypto() {}
    ~Crypto() {}
    DISALLOW_COPY_AND_ASSIGN(Crypto);
};

};  // namespace security

};  // namespace zjchain