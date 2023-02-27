#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"

namespace zjchain {

namespace contract {

class Identity : public ContractInterface {
public:
    Identity(const std::string& create_address);
    virtual ~Identity();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:
    uint64_t gas_cast_{ 3000llu };

    DISALLOW_COPY_AND_ASSIGN(Identity);
};

}  // namespace contract

}  // namespace zjchain
