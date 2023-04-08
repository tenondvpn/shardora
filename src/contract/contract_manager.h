#pragma once

#include <unordered_map>
#include <mutex>

#include "contract/contract_interface.h"
#include "contract/contract_utils.h"
#include "protos/contract.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace contract {

class ContractManager {
public:
    ContractManager();
    ~ContractManager();
    void Init(std::shared_ptr<security::Security>& secptr);
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:
    std::unordered_map<std::string, ContractInterfacePtr> contract_map_;

    DISALLOW_COPY_AND_ASSIGN(ContractManager);
};

}  // namespace contract

}  // namespace zjchain
