#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"

namespace zjchain {

namespace contract {

class ContractAltBn128G1Mul : public ContractInterface {
public:
    ContractAltBn128G1Mul(const std::string& create_address);
    virtual ~ContractAltBn128G1Mul();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    void CallBn128Mul(const std::string& bytes_data);

private:
    int64_t gas_cast_{ 6000ll };

    DISALLOW_COPY_AND_ASSIGN(ContractAltBn128G1Mul);
};

}  // namespace contract

}  // namespace zjchain
