#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"
#include "security/security.h"

namespace zjchain {

namespace contract {

class Ecrecover : public ContractInterface {
public:
    Ecrecover(
        const std::string& create_address,
        std::shared_ptr<security::Security>& security_ptr);
    virtual ~Ecrecover();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:
    uint64_t gas_cast_{ 3000llu };
    std::shared_ptr<security::Security> security_ptr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(Ecrecover);
};

}  // namespace contract

}  // namespace zjchain
