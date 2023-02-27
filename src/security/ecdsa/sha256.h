#pragma once

#include <vector>
#include <cassert>

#include <openssl/sha.h>
#include "common/utils.h"
#include "security/ecdsa/crypto_utils.h"

namespace zjchain {

namespace security {

class Sha256 {
public:
    Sha256() {
        Reset();
    }

    ~Sha256() {}

    void Update(const std::string& input) {
        if (input.size() == 0) {
            assert(false);
            return;
        }
        SHA256_Update(&context_, (unsigned char*)(input.c_str()), input.size());
    }

    void Update(const std::string& input, uint32_t offset, uint32_t size) {
        if (input.size() <= (offset + size)) {
            assert(false);
            return;
        }
        SHA256_Update(&context_, (unsigned char*)(input.c_str() + offset), size);
    }

    std::string Finalize() {
        SHA256_Final(output_, &context_);
        return std::string((char*)output_, sizeof(output_));
    }

    void Reset() {
        memset(output_, 0, sizeof(output_));
        SHA256_Init(&context_);
    }

private:
    static const unsigned int kHashOutputSize = 32u;

    SHA256_CTX context_{};
    unsigned char output_[kHashOutputSize];

    DISALLOW_COPY_AND_ASSIGN(Sha256);
};

}  // namespace security

}  // namespace zjchain
