#include "contract/contract_create2.h"
#include "common/hash.h"
#include "common/encode.h"

namespace shardora {

namespace contract {

ContractCreate2::ContractCreate2(const std::string& create_address)
    : ContractInterface(create_address) {}

ContractCreate2::~ContractCreate2() {}

int ContractCreate2::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {

    // 1. 参数检查
    if (param.data.size() < 32) {
        SHARDORA_ERROR("CREATE2 param data size < 32, size=%lu", param.data.size());
        return kContractError;
    }

    // 2. 提取 Salt 和 InitCode
    std::string salt((char*)param.create2_salt.bytes, sizeof(param.create2_salt.bytes));
    std::string init_code = param.data;

    // 3. 动态 Gas 计算（严格符合 EIP-1014）
    // base = 32000
    // hash_cost = 6 * ceil(init_code.size() / 32)
    size_t word_count = (init_code.size() + 31) / 32;
    uint64_t dynamic_gas = 32000 + 6 * word_count;

    if (res->gas_left < dynamic_gas) {  // 使用传入的 gas 参数进行检查
        SHARDORA_ERROR("CREATE2 insufficient gas: provided=%lu, required=%lu", res->gas_left, dynamic_gas);
        return kContractError;
    }

    // 4. 计算 CREATE2 地址 (EIP-1014)
    // keccak256(0xff + sender(20) + salt(32) + keccak256(init_code)(32))
    std::string code_hash = common::Hash::keccak256(init_code);

    std::string buffer;
    buffer.reserve(85);
    buffer.push_back(static_cast<char>(0xff));                    // 1 byte

    // 确保 sender 是 20 字节（从 param.from 取最后 20 字节）
    std::string sender = param.from.size() >= 20 
                       ? param.from.substr(param.from.size() - 20) 
                       : param.from;
    if (sender.size() < 20) {
        sender = std::string(20 - sender.size(), '\0') + sender;  // 左补零（极少发生）
    }
    buffer.append(sender);

    buffer.append(salt);                                          // 32 bytes
    buffer.append(code_hash);                                     // 32 bytes

    std::string final_hash = common::Hash::keccak256(buffer);

    // 取最后 20 字节作为新合约地址
    std::string new_address = final_hash.substr(12, 20);

    // 5. 设置 evmc_result 返回值
    // 返回 32 字节地址（左边 12 字节为 0，右边 20 字节为地址）—— 符合多数预计算场景预期
    res->output_data = new uint8_t[32];
    memset((void*)res->output_data, 0, 32);
    memcpy(static_cast<uint8_t*>(const_cast<uint8_t*>(res->output_data)) + 12, new_address.data(), 20);
    res->output_size = 32;

    // 设置 create_address（如果上层需要记录实际创建的地址）
    if (create_address_.size() >= 20) {
        memcpy(res->create_address.bytes,
               create_address_.data() + (create_address_.size() - 20),
               20);
    } else {
        memset(res->create_address.bytes, 0, 20);
    }

    memcpy(res->create_address.bytes, new_address.data(), 20);
    res->status_code = EVMC_SUCCESS;
    res->gas_left -= dynamic_gas;   // 扣除实际消耗的 Gas

    SHARDORA_DEBUG("CREATE2 success - predicted_address: %s, sender: %s, salt: %s, "
        "init_code_len: %lu, gas: %lu, gas left: %lu, dy gas: %lu, data: %s",
        common::Encode::HexEncode(new_address).c_str(),
        common::Encode::HexEncode(sender).c_str(),
        common::Encode::HexEncode(salt).c_str(),
        init_code.size(),
        gas,
        res->gas_left,
        dynamic_gas,
        common::Encode::HexEncode(param.data).c_str());

    return kContractSuccess;
}

}  // namespace contract

}  // namespace shardora
