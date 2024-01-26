#include "common/limit_heap.h"

namespace zjchain {

namespace common {

template<>
uint64_t MinHeapUniqueVal(const std::string& val) {
    return common::Hash::Hash64(val);
}

template<>
uint64_t MinHeapUniqueVal(const uint64_t& val) {
    return val;
}

template<>
uint64_t MinHeapUniqueVal(const int64_t& val) {
    return val;
}

template<>
uint64_t MinHeapUniqueVal(const uint32_t& val) {
    return val;
}

template<>
uint64_t MinHeapUniqueVal(const int32_t& val) {
    return val;
}

}  // namespace common

}  // namespace zjchain
