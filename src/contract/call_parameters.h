#pragma once

#include <functional>

#include "contract/contract_utils.h"

namespace zjchain {

namespace zjcvm {
    class ZjchainHost;
}

namespace contract {

class VMFace;
class ExtVMFace;
using OnOpFunc = std::function<void(
    uint64_t,
    uint64_t,
    uint8_t,
    uint64_t,
    uint64_t,
    uint64_t,
    VMFace const*,
    ExtVMFace const*)>;

struct CallParameters {
    std::string from;
    std::string to;
    std::string code_address;
    uint64_t value;
    uint64_t apparent_value;
    uint64_t gas;
    std::string data;
    OnOpFunc on_op;
    zjcvm::ZjchainHost* zjc_host;
};

}  // namespace contact

}  // namespace zjchain
