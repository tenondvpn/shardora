#include "contract/contract_ripemd160.h"

#include <openssl/ripemd.h>
#include <cstring>

#include "common/encode.h"
#include "common/log.h"

namespace shardora {

namespace contract {

Ripemd160::Ripemd160(const std::string& create_address)
        : ContractInterface(create_address) {}

Ripemd160::~Ripemd160() {}

int Ripemd160::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    // Gas cost: 600 base + 120 per 32-byte word (rounds up)
    int64_t gas_used = ComputeGasUsed(600, 120, static_cast<uint32_t>(param.data.size()));
    if (res->gas_left < gas_used) {
        SHARDORA_DEBUG("ripemd160: out of gas, required=%ld, available=%ld",
            gas_used, res->gas_left);
        return kContractError;
    }

    // Compute RIPEMD-160 via OpenSSL
    uint8_t digest[RIPEMD160_DIGEST_LENGTH];  // 20 bytes
    RIPEMD160(
        reinterpret_cast<const uint8_t*>(param.data.data()),
        param.data.size(),
        digest);

    // Output: 32 bytes — 12 zero bytes + 20-byte digest (left-padded to 32)
    static constexpr size_t kOutputSize = 32;
    static constexpr size_t kPadding    = kOutputSize - RIPEMD160_DIGEST_LENGTH;  // 12

    uint8_t* out = new uint8_t[kOutputSize];
    memset(out, 0, kPadding);
    memcpy(out + kPadding, digest, RIPEMD160_DIGEST_LENGTH);
    res->output_data = out;
    res->output_size = kOutputSize;

    memcpy(res->create_address.bytes,
        create_address_.c_str(),
        sizeof(res->create_address.bytes));

    res->gas_left -= gas_used;

    SHARDORA_DEBUG("ripemd160: input_len=%zu, digest=%s",
        param.data.size(),
        common::Encode::HexEncode(std::string((char*)digest, RIPEMD160_DIGEST_LENGTH)).c_str());

    return kContractSuccess;
}

}  // namespace contract

}  // namespace shardora
