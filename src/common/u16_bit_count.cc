#include "common/u16_bit_count.h"

namespace zjchain {

namespace common {

U16BitCount* U16BitCount::Instance() {
    static U16BitCount ins;
    return &ins;
}
    
};  // namespace common

};  // namespace zjchain