#include "zjcvm/zjc_host.h"

#include <evmc/hex.hpp>

#include "block/account_manager.h"
#include "common/encode.h"
#include "common/log.h"
#include "contract/call_parameters.h"
#include "contract/contract_manager.h"
#include "protos/prefix_db.h"
#include "zjcvm/execution.h"
#include "zjcvm/zjcvm_utils.h"

namespace zjchain {

namespace zjcvm {

bool ZjchainHost::account_exists(const evmc::address& addr) const noexcept {
    return Execution::Instance()->IsAddressExists(
        thread_idx_,
        std::string((char*)addr.bytes, sizeof(addr.bytes)));
}

evmc::bytes32 ZjchainHost::get_storage(
        const evmc::address& addr,
        const evmc::bytes32& key) const noexcept {
    return Execution::Instance()->GetStorage(thread_idx_, addr, key);
}

evmc_storage_status ZjchainHost::set_storage(
        const evmc::address& addr,
        const evmc::bytes32& key,
        const evmc::bytes32& value) noexcept {
    // just set temporary map storage, when commit set to db and block
    std::string id((char*)addr.bytes, sizeof(addr.bytes));
    std::string key_str((char*)key.bytes, sizeof(key.bytes));
    std::string val_str((char*)value.bytes, sizeof(value.bytes));
    ZJC_DEBUG("zjcvm set storage called, id: %s, key: %s, value: %s",
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key_str).c_str(),
        common::Encode::HexEncode(val_str).c_str());
    auto it = accounts_.find(addr);
    if (it == accounts_.end()) {
        accounts_[addr] = MockedAccount();
        it = accounts_.find(addr);
    }

    auto& old = it->second.storage[key];
    if (old.value == value)
        return EVMC_STORAGE_MODIFIED_RESTORED;

    evmc_storage_status status{};
    if (!old.dirty) {
        old.dirty = true;
        if (!old.value)
            status = EVMC_STORAGE_ADDED;
        else if (value)
            status = EVMC_STORAGE_MODIFIED;
        else
            status = EVMC_STORAGE_DELETED;
    } else {
        status = EVMC_STORAGE_MODIFIED_RESTORED;
    }

    old.value = value;
    return status;
}

evmc::uint256be ZjchainHost::get_balance(const evmc::address& addr) const noexcept {
    // don't use real balance
    auto iter = account_balance_.find(addr);
    if (iter == account_balance_.end()) {
        return {};
    }

    return iter->second;
}

size_t ZjchainHost::get_code_size(const evmc::address& addr) const noexcept {
    std::string code;
    if (Execution::Instance()->GetStorage(thread_idx_, addr, protos::kFieldBytesCode, &code)) {
        return code.size();
    }

    return 0;
}

evmc::bytes32 ZjchainHost::get_code_hash(const evmc::address& addr) const noexcept {
    std::string code;
     

    std::string code_hash = common::Hash::keccak256(code);
    evmc::bytes32 tmp_val{};
    memcpy(tmp_val.bytes, code_hash.c_str(), sizeof(tmp_val.bytes));
    return tmp_val;
}

size_t ZjchainHost::copy_code(
        const evmc::address& addr,
        size_t code_offset,
        uint8_t* buffer_data,
        size_t buffer_size) const noexcept {
    std::string code;
    if (!Execution::Instance()->GetStorage(thread_idx_, addr, protos::kFieldBytesCode, &code)) {
        return 0;
    }

    if (code_offset >= code.size()) {
        return 0;
    }

    const auto n = (std::min)(buffer_size, code.size() - code_offset);
    if (n > 0) {
        std::copy_n(&code[code_offset], n, buffer_data);
    }

    return n;
}

bool ZjchainHost::selfdestruct(
        const evmc::address& addr,
        const evmc::address& beneficiary) noexcept {
    recorded_selfdestructs_.push_back({ addr, beneficiary });
    return true;
}

evmc::Result ZjchainHost::call(const evmc_message& msg) noexcept {
    contract::CallParameters params;
    params.zjc_host = this;
    params.gas = msg.gas;
    params.apparent_value = zjcvm::EvmcBytes32ToUint64(msg.value);
    params.value = msg.kind == EVMC_DELEGATECALL ? 0 : params.apparent_value;
    params.from = std::string((char*)msg.sender.bytes, sizeof(msg.sender.bytes));
    params.code_address = std::string(
        (char*)msg.recipient.bytes,
        sizeof(msg.recipient.bytes));
    params.to = msg.kind == EVMC_CALL ? params.code_address : my_address_;
    params.data = std::string((char*)msg.input_data, msg.input_size);
    params.on_op = {};
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = msg.gas;
    std::cout << "host called kind: " << msg.kind
        << ", from: " << common::Encode::HexEncode(params.from)
        << ", to: " << common::Encode::HexEncode(params.to) << std::endl;
    if (contract_mgr_->call(
            params,
            gas_price_,
            origin_address_,
            raw_result) != contract::kContractNotExists) {
    } else {
        std::string id = std::string((char*)msg.code_address.bytes, sizeof(msg.code_address.bytes));
        auto acc_info = acc_mgr_->GetAccountInfo(thread_idx_, acc_info);
        if (acc_info == nullptr || acc_info->bytes_code().empty()) {
            evmc_res.status_code = EVMC_REVERT;
            ZJC_WARN("get call bytes code failed: %s, field: %s",
                common::Encode::HexEncode(id).c_str(),
                protos::kFieldBytesCode.c_str());
            return evmc_res;
        }

        ZJC_DEBUG("get call bytes code success: %s, field: %s",
            common::Encode::HexEncode(id).c_str(),
            protos::kFieldBytesCode.c_str());
        ++depth_;
        int res_status = zjcvm::Execution::Instance()->execute(
            acc_info->bytes_code(),
            params.data,
            params.from,
            params.to,
            origin_address_,
            params.apparent_value,
            params.gas,
            depth_,
            zjcvm::kJustCall,
            *this,
            &evmc_res);
    }

    if (params.value > 0) {
        uint64_t from_balance = EvmcBytes32ToUint64(get_balance(msg.sender));
        if (from_balance < params.value) {
            evmc_res.status_code = EVMC_INSUFFICIENT_BALANCE;
        } else {
            std::string from_str = std::string((char*)msg.sender.bytes, sizeof(msg.sender.bytes));
            std::string dest_str = std::string((char*)msg.recipient.bytes, sizeof(msg.recipient.bytes));
            auto sender_iter = to_account_value_.find(from_str);
            if (sender_iter == to_account_value_.end()) {
                to_account_value_[from_str] = std::unordered_map<std::string, uint64_t>();
                to_account_value_[from_str][dest_str] = params.value;
            } else {
                auto iter = sender_iter->second.find(dest_str);
                if (iter != sender_iter->second.end()) {
                    sender_iter->second[dest_str] += params.value;
                } else {
                    sender_iter->second[dest_str] = params.value;
                }
            }

            ZJC_DEBUG("contract transfer from: %s, to: %s, amount: %lu",
                common::Encode::HexEncode(from_str).c_str(),
                common::Encode::HexEncode(dest_str).c_str(),
                params.value);
        }
    }
    
    return evmc_res;
}

evmc_tx_context ZjchainHost::get_tx_context() const noexcept {
    return tx_context_;
}

evmc::bytes32 ZjchainHost::get_block_hash(int64_t block_number) const noexcept {
    return block_hash_;
}

void ZjchainHost::emit_log(const evmc::address& addr,
                const uint8_t* data,
                size_t data_size,
                const evmc::bytes32 topics[],
                size_t topics_count) noexcept {
    std::string id((char*)addr.bytes, sizeof(addr.bytes));
    std::string str_data((char*)data, data_size);
    std::string topics_str;
    for (uint32_t i = 0; i < topics_count; ++i) {
        topics_str += std::string((char*)topics[i].bytes, sizeof(topics[i].bytes)) + ", ";
    }

    std::cout << "log called id: " << common::Encode::HexEncode(id)
        << ", data: " << common::Encode::HexEncode(str_data)
        << ", topics: " << common::Encode::HexEncode(topics_str) << std::endl;
    recorded_logs_.push_back({ addr, {data, data_size}, {topics, topics + topics_count} });
}

void ZjchainHost::AddTmpAccountBalance(const std::string& address, uint64_t balance) {
    evmc::address addr;
    memcpy(
        addr.bytes,
        address.c_str(),
        sizeof(addr.bytes));
    evmc::bytes32 tmp_val{};
    Uint64ToEvmcBytes32(tmp_val, balance);
    account_balance_[addr] = tmp_val;
}


int ZjchainHost::SaveKeyValue(
        const std::string& id,
        const std::string& key,
        const std::string& val) {
    auto addr = evmc::address{};
    memcpy(addr.bytes, id.c_str(), id.size());
    CONTRACT_DEBUG("zjcvm set storage called, id: %s, key: %s, value: %s",
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key).c_str(),
        common::Encode::HexEncode(val).c_str());
    auto it = accounts_.find(addr);
    if (it == accounts_.end()) {
        accounts_[addr] = MockedAccount();
        it = accounts_.find(addr);
    }

    auto& old = it->second.str_storage[key];
    old.str_val = val;
    return kZjcvmSuccess;
}

int ZjchainHost::GetKeyValue(const std::string& id, const std::string& key_str, std::string* val) {
    auto addr = evmc::address{};
    memcpy(addr.bytes, id.c_str(), id.size());
    if (!Execution::Instance()->GetStorage(thread_idx_, addr, key_str, val)) {
        return kZjcvmError;
    }

    return kZjcvmSuccess;
}

evmc_access_status ZjchainHost::access_account(const evmc::address& addr) noexcept {
    if (Execution::Instance()->AddressWarm(thread_idx_, addr)) {
        return EVMC_ACCESS_WARM;
    }

    return EVMC_ACCESS_COLD;
}

evmc_access_status ZjchainHost::access_storage(
        const evmc::address& addr,
        const evmc::bytes32& key) noexcept {
    if (Execution::Instance()->StorageKeyWarm(thread_idx_, addr, key)) {
        return EVMC_ACCESS_WARM;
    }

    return EVMC_ACCESS_COLD;
}
}  // namespace zjcvm

}  // namespace zjchain
