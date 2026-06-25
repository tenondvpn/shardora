#pragma once

#include "contract/contract_interface.h"
#include "common/hash.h"

namespace shardora {

namespace contract {

class ContractCreate2 : public ContractInterface {
public:
    // 构造函数，传入全 0 地址作为合约标识
    ContractCreate2(const std::string& create_address);
    virtual ~ContractCreate2();

    virtual int call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) override;

private:
    // CREATE2 的基础消耗通常比 Ecrecover 高，包含哈希计算开销
    int64_t gas_cast_{ 32000ll }; 

    DISALLOW_COPY_AND_ASSIGN(ContractCreate2);
};

}  // namespace contract

}  // namespace shardora