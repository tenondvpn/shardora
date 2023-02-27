#pragma once

#include <evmc/evmc.h>

#include "common/encode.h"
#include "common/log.h"
#include "common/utils.h"

namespace zjchain {

namespace zjcvm {

enum ZjcvmErrorCode {
    kZjcvmSuccess = 0,
    kZjcvmError = 1,
    kZjcvmKeyExsits = 2,
    kZjcvmKeyAdded = 3,
    kZjcvmBlockReloaded = 4,
    kZjcvmContractNotExists = 5,
};

enum ContractCallMode {
    kJustCall = 0,
    kJustCreate = 1,
    kCreateAndCall = 2,
};

static const std::string kContractHead = common::Encode::HexDecode("60806040");
static const uint32_t kContractCallMaxDepth = 1024u;

inline static bool IsContractBytesCode(const std::string& bytes_code) {
    return memcmp(kContractHead.c_str(), bytes_code.c_str(), kContractHead.size()) == 0;
}

inline static void Uint64ToEvmcBytes32(evmc_bytes32& bytes32, uint64_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        bytes32.bytes[sizeof(bytes32.bytes) - 1 - i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

inline static uint64_t EvmcBytes32ToUint64(const evmc_bytes32& bytes32) {
    uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(uint64_t); ++i) {
        value += (((uint64_t)(bytes32.bytes[sizeof(bytes32.bytes) - 1 - i])) << (8 * i));
    }

    return value;
}

}  // namespace zjcvm

}  // namespace zjchain

#if __cplusplus
extern "C" {
#endif

    const struct evmc_host_interface* tvm_host_get_interface();

    struct evmc_host_context* tvm_host_create_context(struct evmc_tx_context tx_context);

    void tvm_host_destroy_context(struct evmc_host_context* context);

#if __cplusplus
}
#endif
