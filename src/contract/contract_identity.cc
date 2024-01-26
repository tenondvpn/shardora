#include "contract/contract_identity.h"

namespace zjchain {

namespace contract {

Identity::Identity(const std::string& create_address)
        : ContractInterface(create_address) {}

Identity::~Identity() {}

int Identity::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    if (param.data.empty()) {
        return kContractError;
    }

    int64_t gas_used = ComputeGasUsed(15, 3, param.data.size());
    if (res->gas_left < gas_used) {
        return kContractError;
    }

    res->output_data = new uint8_t[param.data.size()];
    memcpy((void*)res->output_data, param.data.c_str(), param.data.size());
    res->output_size = param.data.size();
    memcpy(res->create_address.bytes,
        create_address_.c_str(),
        sizeof(res->create_address.bytes));
    res->gas_left -= gas_used;
    return kContractSuccess;
}

}  // namespace contract

}  // namespace zjchain
