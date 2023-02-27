#include "contract/contract_sha256.h"

#include "common/hash.h"

namespace zjchain {

namespace contract {

ContractSha256::ContractSha256(const std::string& create_address)
        : ContractInterface(create_address) {}

ContractSha256::~ContractSha256() {}

int ContractSha256::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    if (param.data.empty()) {
        return kContractError;
    }

    int64_t gas_used = ComputeGasUsed(60, 12, param.data.size());
    if (res->gas_left < gas_used) {
        return kContractError;
    }

    std::string sha256 = common::Hash::Sha256(param.data);
    res->output_data = new uint8_t[sha256.size()];
    memcpy((void*)res->output_data, sha256.c_str(), sha256.size());
    res->output_size = sha256.size();
    memcpy(res->create_address.bytes,
        create_address_.c_str(),
        sizeof(res->create_address.bytes));
    res->gas_left -= gas_used;
    return kContractSuccess;
}

}  // namespace contract

}  // namespace zjchain
