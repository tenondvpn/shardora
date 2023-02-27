#include "contract/contract_ecrecover.h"

#include "common/hash.h"
#include "security/security.h"
#include "security/ecdsa/secp256k1.h"

namespace zjchain {

namespace contract {

Ecrecover::Ecrecover(
        const std::string& create_address,
        std::shared_ptr<security::Security>& security_ptr)
        : ContractInterface(create_address), security_ptr_(security_ptr) {}

Ecrecover::~Ecrecover() {}

int Ecrecover::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    if (param.gas < gas_cast_) {
        return kContractError;
    }

    if (param.data.size() != 128) {
        return kContractError;
    }

    std::string hash(param.data.c_str(), 32);
    std::string sign(param.data.c_str() + 32, param.data.size() - 32);
    std::string pubkey = security::Secp256k1::Instance()->RecoverForContract(sign, hash);
    std::string addr_sha3 = common::Hash::keccak256(pubkey);
    res->output_data = new uint8_t[addr_sha3.size()];
    memcpy((void*)res->output_data, addr_sha3.c_str(), addr_sha3.size());
    res->output_size = addr_sha3.size();
    memcpy(res->create_address.bytes,
        create_address_.c_str(),
        sizeof(res->create_address.bytes));
    res->gas_left -= gas_cast_;
    return kContractSuccess;
}

}  // namespace contract

}  // namespace zjchain
