#pragma once

#include <string>

#include "common/utils.h"

namespace zjchain {

namespace common {

class Hash {
public:
    static uint32_t Hash32(const std::string& str);
    static uint64_t Hash64(const std::string& str);
    static std::string Hash128(const std::string& str);
    static std::string Hash192(const std::string& str);
    static std::string Hash256(const std::string& str);
    static std::string Sha256(const std::string& str);
    static std::string ripemd160(const std::string& str);
    static std::string keccak256(const std::string& str);

private:
    Hash() {}
    ~Hash() {}

    static const uint32_t kHashSeedU32 = 5623453345u;
    static const uint64_t kHashSeed1 = 23456785675590ull;
    static const uint64_t kHashSeed2 = 45654789542344ull;
    static const uint64_t kHashSeed3 = 75464565745625ull;
    static const uint64_t kHashSeed4 = 64556455234534ull;
    static const unsigned int kHashOutputSize = 32u;

    DISALLOW_COPY_AND_ASSIGN(Hash);
};

}  // namespace common

}  // namespace zjchain
