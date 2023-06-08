#include "zjcvm/execution.h"

#include "common/encode.h"
#include "common/global_info.h"
#include "evmone/evmone.h"
#include "evmc/loader.h"
#include "evmc/hex.hpp"
#include "evmc/evmc.h"
#include "evmc/mocked_host.hpp"
#include "security/security_utils.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace zjchain {

namespace zjcvm {

Execution::Execution() {}

Execution::~Execution() {
    if (address_exists_set_ != nullptr) {
        delete[] address_exists_set_;
    }

    if (storage_map_ != nullptr) {
        delete[] storage_map_;
    }
}

Execution* Execution::Instance() {
    static Execution ins;
    return &ins;
}

void Execution::Init(std::shared_ptr<db::Db>& db) {
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    evm_ = evmc::VM{ evmc_create_evmone()};
    uint32_t thread_count = common::GlobalInfo::Instance()->message_handler_thread_count() - 1;
    address_exists_set_ = new common::StringUniqueSet<10240, 16>[thread_count];
    storage_map_ = new common::UniqueMap<std::string, std::string, 10240, 16>[thread_count];
}

int Execution::execute(
        const std::string& bytes_code,
        const std::string& str_input,
        const std::string& from_address,
        const std::string& to_address,
        const std::string& origin_address,
        uint64_t value,
        uint64_t gas_limit,
        uint32_t depth,
        uint32_t call_mode,
        ZjchainHost& host,
        evmc::Result* out_res) {
    const size_t code_size = bytes_code.size();
    if (code_size <= kContractHead.size() ||
            from_address.size() != security::kUnicastAddressLength ||
            to_address.size() != security::kUnicastAddressLength ||
            depth >= kContractCallMaxDepth ||
            gas_limit <= 0) {
        ZJC_DEBUG("invalid params!");
        return kZjcvmError;
    }

    int64_t gas = gas_limit;
    auto rev = EVMC_LATEST_STABLE_REVISION;
    auto create_gas = gas_limit;

    evmc_message msg{};
    msg.gas = gas;
    msg.input_data = (uint8_t*)str_input.c_str();
    msg.input_size = str_input.size();
    Uint64ToEvmcBytes32(msg.value, value);
    memcpy(
        msg.sender.bytes,
        from_address.c_str(),
        sizeof(msg.sender.bytes));
    memcpy(
        msg.recipient.bytes,
        to_address.c_str(),
        sizeof(msg.recipient.bytes));
    const uint8_t* exec_code_data = nullptr;
    size_t exec_code_size = 0;
    if (call_mode == kJustCreate || call_mode == kCreateAndCall) {
//         evmc_message create_msg{};
        msg.kind = EVMC_CREATE;
//         create_msg.recipient = msg.recipient;
//         create_msg.gas = create_gas;
        *out_res = evm_.execute(
            host,
            rev,
            msg,
            (uint8_t*)bytes_code.c_str(),
            bytes_code.size());
        if (out_res->status_code != EVMC_SUCCESS) {
            const auto gas_used = msg.gas - out_res->gas_left;
            ZJC_ERROR("out_res->status_code != EVMC_SUCCESS.nResult: %d, gas_used: %lu, gas limit: %lu, codes: %s",
                out_res->status_code, gas_used, create_gas, common::Encode::HexEncode(bytes_code).c_str());
            return out_res->status_code;
        } else {
            const auto gas_used = msg.gas - out_res->gas_left;
            ZJC_ERROR("out_res->status_code != EVMC_SUCCESS.nResult: %d, gas_used: %lu, gas limit: %lu, codes: %s",
                out_res->status_code, gas_used, create_gas, common::Encode::HexEncode(bytes_code).c_str());
        }

        host.create_bytes_code_ = std::string((char*)out_res->output_data, out_res->output_size);
        if (call_mode == kJustCreate) {
            return kZjcvmSuccess;
        }

        msg.gas = out_res->gas_left;
        auto& created_account = host.accounts_[msg.recipient];
        created_account.code = evmc::bytes(out_res->output_data, out_res->output_size);
        exec_code_data = created_account.code.data();
        exec_code_size = created_account.code.size();
    } else {
        exec_code_data = (uint8_t*)bytes_code.c_str();
        exec_code_size = bytes_code.size();
    }

    ZJC_DEBUG("now call contract, msg sender: %s", common::Encode::HexEncode(std::string((char*)msg.sender.bytes, 20)).c_str());
    *out_res = evm_.execute(host, rev, msg, exec_code_data, exec_code_size);
    return kZjcvmSuccess;
}

}  // namespace zjcvm

}  //namespace zjchain
