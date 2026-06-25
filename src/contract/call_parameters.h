#pragma once

#include <functional>

#include <evmc/evmc.h>

#include "contract/contract_utils.h"

namespace shardora {

namespace shardoravm {
    class ShardorahainHost;
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
    evmc_bytes32 create2_salt;
    const uint8_t* code;
    size_t code_size;
    shardoravm::ShardorahainHost* shardora_host;
};

}  // namespace contact

}  // namespace shardora
