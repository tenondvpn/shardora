#pragma once

#include <unordered_map>
#include <limits>

#include "common/utils.h"

namespace zjchain {

namespace common {

class U16BitCount {
public:
    static U16BitCount* Instance();
    uint32_t DiffCount(uint16_t num) {
        return u16_bit_count_map_[num];
    }

private:
    U16BitCount() {
        memset(u16_bit_count_map_, 0, sizeof(u16_bit_count_map_));
        for (int i = 0; i < (std::numeric_limits<uint16_t>::max)(); ++i) {
            u16_bit_count_map_[i] = (i & 1) + u16_bit_count_map_[i / 2];
        }
    }

    ~U16BitCount() {}

    uint32_t u16_bit_count_map_[(std::numeric_limits<uint16_t>::max)()];
};

};  // namespace common

};  // namespace zjchain