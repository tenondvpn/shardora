#pragma once

#include <unordered_map>
#include <limits>

#include "common/utils.h"

namespace shardora {

namespace common {

class U16BitCount {
public:
    static U16BitCount* Instance();
    uint32_t DiffCount(uint16_t num) {
        return u16_bit_count_map_[num];
    }

private:
    static constexpr size_t kMapSize =
        static_cast<size_t>((std::numeric_limits<uint16_t>::max)()) + 1u;

    U16BitCount() {
        memset(u16_bit_count_map_, 0, sizeof(u16_bit_count_map_));
        for (size_t i = 0; i < kMapSize; ++i) {
            u16_bit_count_map_[i] = static_cast<uint32_t>((i & 1u) + u16_bit_count_map_[i >> 1u]);
        }
    }

    ~U16BitCount() {}

    uint32_t u16_bit_count_map_[kMapSize];
};

};  // namespace common

};  // namespace shardora