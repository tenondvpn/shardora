#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"

namespace zjchain {

namespace contract {

class ContractaltBn128PairingProduct : public ContractInterface {
public:
    ContractaltBn128PairingProduct(const std::string& create_address);
    virtual ~ContractaltBn128PairingProduct();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:

    DISALLOW_COPY_AND_ASSIGN(ContractaltBn128PairingProduct);
};

}  // namespace contract

}  // namespace zjchain
