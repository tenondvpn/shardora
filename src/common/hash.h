#pragma once

#include <string>
#include <array>
#include <sstream>
#include <iomanip>

#include "common/utils.h"

namespace shardora {

namespace common {

struct HashValue {
    std::array<uint8_t, 32> data;

    HashValue(const std::string& str) {
        if (str.size() != 64) {
            throw std::invalid_argument("Invalid string length for HashValue");
        }

        for (size_t i = 0; i < 32; ++i) {
            data[i] = static_cast<uint8_t>(std::stoi(str.substr(i * 2, 2), nullptr, 16));
        }
    }    

    std::string to_string() const {
        std::ostringstream oss;
        for (uint8_t byte : data) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return oss.str();
    }
};

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

    static const uint32_t kHashSeedU32 = 623453345u;
    static const uint64_t kHashSeed1 = 23456785675590ull;
    static const uint64_t kHashSeed2 = 45654789542344ull;
    static const uint64_t kHashSeed3 = 75464565745625ull;
    static const uint64_t kHashSeed4 = 64556455234534ull;
    static const unsigned int kHashOutputSize = 32u;

    DISALLOW_COPY_AND_ASSIGN(Hash);
};

}  // namespace common

}  // namespace shardora
