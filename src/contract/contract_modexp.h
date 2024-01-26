#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"

namespace zjchain {

namespace contract {

class Modexp : public ContractInterface {
public:
    Modexp(const std::string& create_address);
    virtual ~Modexp();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:
    uint64_t GetGasPrice(const std::string& data);

    DISALLOW_COPY_AND_ASSIGN(Modexp);
};

}  // namespace contract

}  // namespace zjchain
