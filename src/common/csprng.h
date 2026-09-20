#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "common/hash.h"

namespace shardora {

namespace common {

class CsprngU64 {
public:
    using result_type = uint64_t;

    explicit CsprngU64(uint64_t seed) : counter_(0) {
        memcpy(&seed_, &seed, sizeof(uint64_t));
    }

    uint64_t operator()() {
        char buf[16];
        memcpy(buf, &seed_, 8);
        memcpy(buf + 8, &counter_, 8);
        ++counter_;
        std::string h = Hash::Sha256(std::string(buf, 16));
        uint64_t val = 0;
        memcpy(&val, h.data(), sizeof(uint64_t));
        return val;
    }

    static constexpr uint64_t min() { return 0; }
    static constexpr uint64_t max() { return std::numeric_limits<uint64_t>::max(); }

private:
    uint64_t seed_;
    uint64_t counter_;
};

}  // namespace common

}  // namespace shardora
