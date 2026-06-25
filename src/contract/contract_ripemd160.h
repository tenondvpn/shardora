#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"

namespace shardora {

namespace contract {

// Ethereum precompile 0x03: RIPEMD-160 hash
// Input : arbitrary bytes
// Output: 32 bytes — 12 zero bytes followed by the 20-byte RIPEMD-160 digest
// Gas   : 600 base + 120 per 32-byte word (EIP-2028 compatible)
class Ripemd160 : public ContractInterface {
public:
    explicit Ripemd160(const std::string& create_address);
    virtual ~Ripemd160();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) override;

private:
    DISALLOW_COPY_AND_ASSIGN(Ripemd160);
};

}  // namespace contract

}  // namespace shardora
