#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"

namespace zjchain {

namespace contract {

class Blake2Compression : public ContractInterface {
public:
    Blake2Compression(const std::string& create_address);
    virtual ~Blake2Compression();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:
    uint64_t gas_cast_{ 3000llu };

    DISALLOW_COPY_AND_ASSIGN(Blake2Compression);
};

}  // namespace contract

}  // namespace zjchain
