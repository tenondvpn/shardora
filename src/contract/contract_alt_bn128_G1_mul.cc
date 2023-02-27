#include "contract/contract_alt_bn128_G1_mul.h"

#include "big_num/snark.h"
#include "big_num/libsnark.h"

namespace zjchain {

namespace contract {

ContractAltBn128G1Mul::ContractAltBn128G1Mul(const std::string& create_address)
        : ContractInterface(create_address) {}

ContractAltBn128G1Mul::~ContractAltBn128G1Mul() {}

int ContractAltBn128G1Mul::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    if (param.data.empty()) {
        return kContractError;
    }

    if (res->gas_left < gas_cast_) {
        return kContractError;
    }

    bytesConstRef bytes_ref((byte*)param.data.c_str(), param.data.size());
    std::pair<bool, bytes> mul_res = alt_bn128_G1_mul(bytes_ref);
    if (!mul_res.first) {
        return kContractError;
    }

    res->output_data = new uint8_t[mul_res.second.size()];
    memcpy((void*)res->output_data, &mul_res.second.at(0), mul_res.second.size());
    res->output_size = mul_res.second.size();
    memcpy(res->create_address.bytes,
        create_address_.c_str(),
        sizeof(res->create_address.bytes));
    res->gas_left -= gas_cast_;
    return kContractSuccess;
}

void ContractAltBn128G1Mul::CallBn128Mul(const std::string& bytes_data) {
    bytesConstRef bytes_ref((byte*)bytes_data.c_str(), bytes_data.size());
    std::pair<bool, bytes> mul_res = alt_bn128_G1_mul(bytes_ref);
    if (!mul_res.first) {
        CONTRACT_ERROR("call bn 128 mul failed!");
    }
}

}  // namespace contract

}  // namespace zjchain
