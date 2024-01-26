#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"

namespace zjchain {

namespace contract {

class ContractAltBn128G1Add : public ContractInterface {
public:
    ContractAltBn128G1Add(const std::string& create_address);
    virtual ~ContractAltBn128G1Add();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:
    int64_t gas_cast_{ 150ll };

    DISALLOW_COPY_AND_ASSIGN(ContractAltBn128G1Add);
};

}  // namespace contract

}  // namespace zjchain
