#pragma once

#include <string>

#include "common/utils.h"

namespace zjchain {

namespace common {

class Random {
public:
    static int8_t RandomInt8();
    static int16_t RandomInt16();
    static int32_t RandomInt32();
    static int64_t RandomInt64();
    static uint8_t RandomUint8();
    static uint16_t RandomUint16();
    static uint32_t RandomUint32();
    static uint64_t RandomUint64();
    static std::string RandomString(uint32_t size);

private:
    Random();
    ~Random();

    DISALLOW_COPY_AND_ASSIGN(Random);
};

}  // namespace common

}  // namespace zjchain
