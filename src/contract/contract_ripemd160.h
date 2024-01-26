#pragma once

#include "contract/contract_interface.h"
#include "common/tick.h"

namespace zjchain {

namespace contract {

class Ripemd160 : public ContractInterface {
public:
    Ripemd160(const std::string& create_address);
    virtual ~Ripemd160();
    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);

private:
    int CheckDecrytParamsValid(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    int AddParams(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    int Decrypt(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res);
    int TestPbc(const std::string& param);
    int GetValue(
        const CallParameters& param,
        const std::string& key,
        std::string* val,
        evmc_result* res);
    void AddAllParams(
        const CallParameters& param,
        const std::string& val);

    uint64_t gas_cast_{ 3000llu };

    uint64_t pbc_prepair_cast_{ 168164llu };
    uint64_t pbc_exp_cast_{ 279344llu };
    uint64_t pbc_pairing_cast_{ 46308llu };

    DISALLOW_COPY_AND_ASSIGN(Ripemd160);
};

}  // namespace contract

}  // namespace zjchain
