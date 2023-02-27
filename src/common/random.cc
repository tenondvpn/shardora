#include <mutex>
#include <random>
#include <algorithm>
#include <memory>
#ifdef _MSC_VER
#define _WINSOCKAPI_
#include <windows.h>
#endif

#include "common/random.h"

namespace zjchain {

namespace common {

uint32_t& RangeSeed() {
#ifdef _MSC_VER
    static uint32_t seed([] {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return static_cast<uint32_t>(t.LowPart);
    }());
#else
    static uint32_t seed(
        static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));
#endif
    return seed;
}

static std::mt19937 kRandomNumberGenerator(RangeSeed());

template <typename IntType>
IntType RandomInt() {
    static std::uniform_int_distribution<IntType> distribution(
            (std::numeric_limits<IntType>::min)(),
            (std::numeric_limits<IntType>::max)());
    return distribution(kRandomNumberGenerator);
}

template <typename String>
String GetRandomString(size_t size) {
    std::uniform_int_distribution<> distribution(0, 255);
    String random_string(size, 0);
    {
        std::generate(
                random_string.begin(),
                random_string.end(),
                [&] { return distribution(kRandomNumberGenerator);});
    }
    return random_string;
}

int8_t Random::RandomInt8() {
    return RandomInt<int8_t>();
}

int16_t Random::RandomInt16() {
    return RandomInt<int16_t>();
}

int32_t Random::RandomInt32() {
    return RandomInt<int32_t>();
}

int64_t Random::RandomInt64() {
    return RandomInt<int64_t>();
}

uint8_t Random::RandomUint8() {
    return RandomInt<uint8_t>();
}

uint16_t Random::RandomUint16() {
    return RandomInt<uint16_t>();
}

uint32_t Random::RandomUint32() {
    return RandomInt<uint32_t>();
}

uint64_t Random::RandomUint64() {
    return RandomInt<uint64_t>();
}

std::string Random::RandomString(uint32_t size) {
    return GetRandomString<std::string>(size);
}

}  // namespace common

}  // namespace zjchain
