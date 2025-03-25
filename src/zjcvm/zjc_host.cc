#include "zjcvm/zjc_host.h"

#include <evmc/hex.hpp>

#include "block/account_manager.h"
#include "common/encode.h"
#include "common/log.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "contract/call_parameters.h"
#include "contract/contract_manager.h"
#include "protos/prefix_db.h"
#include "zjcvm/execution.h"
#include "zjcvm/zjcvm_utils.h"

namespace shardora {

namespace zjcvm {

bool ZjchainHost::account_exists(const evmc::address& addr) const noexcept {
    ZJC_DEBUG("called 0");
    return Execution::Instance()->IsAddressExists(
        std::string((char*)addr.bytes, sizeof(addr.bytes)));
}

evmc::bytes32 ZjchainHost::get_cached_storage(
        const evmc::address& addr,
        const evmc::bytes32& key) const noexcept {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    std::string id((char*)addr.bytes, sizeof(addr.bytes));
    std::string key_str((char*)key.bytes, sizeof(key.bytes));
    ZJC_DEBUG("view: %lu, 0 0 success get storage addr: %s, "
        "key: %s, val: %s, valid: %d, thread_idx: %d", 
        view_,
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key_str).c_str(),
        "",
        false,
        thread_idx);
    auto it = accounts_.find(addr);
    if (it != accounts_.end()) {
        auto storage_iter = it->second.storage.find(key);
        if (storage_iter != it->second.storage.end()) {
            ZJC_DEBUG("view: %lu, 0 success get storage addr: %s, "
                ": %s, val: %s, valid: %d, thread_idx: %d",
                view_,
                common::Encode::HexEncode(id).c_str(),
                common::Encode::HexEncode(key_str).c_str(),
                common::Encode::HexEncode(
                std::string((char*)storage_iter->second.value.bytes, 32)).c_str(),
                true,
                thread_idx);
            return storage_iter->second.value;
        } else {
            ZJC_DEBUG("key invalid view: %lu, 0 0 success get storage addr: %s, "
                "key: %s, val: %s, valid: %d, thread_idx: %d", 
                view_,
                common::Encode::HexEncode(id).c_str(),
                common::Encode::HexEncode(key_str).c_str(),
                "",
                false,
                thread_idx);
        }
    } else {
        ZJC_DEBUG("addr invalid view: %lu, 0 0 success get storage addr: %s, "
            "key: %s, val: %s, valid: %d, thread_idx: %d", 
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            "",
            false,
            thread_idx);
    }

    evmc::bytes32 tmp_val{};
    return tmp_val;
}

evmc::bytes32 ZjchainHost::get_storage(
        const evmc::address& addr,
        const evmc::bytes32& key) const noexcept {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    std::string id((char*)addr.bytes, sizeof(addr.bytes));
    std::string key_str((char*)key.bytes, sizeof(key.bytes));
    ZJC_DEBUG("view: %lu, 0 0 success get storage addr: %s, "
        "key: %s, val: %s, valid: %d, thread_idx: %d", 
        view_,
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key_str).c_str(),
        "",
        false,
        thread_idx);
    auto it = accounts_.find(addr);
    if (it != accounts_.end()) {
        auto storage_iter = it->second.storage.find(key);
        if (storage_iter != it->second.storage.end()) {
            ZJC_DEBUG("view: %lu, 0 success get storage addr: %s, "
                ": %s, val: %s, valid: %d, thread_idx: %d",
                view_,
                common::Encode::HexEncode(id).c_str(),
                common::Encode::HexEncode(key_str).c_str(),
                common::Encode::HexEncode(
                std::string((char*)storage_iter->second.value.bytes, 32)).c_str(),
                true,
                thread_idx);
            return storage_iter->second.value;
        } else {
            ZJC_DEBUG("key invalid view: %lu, 0 0 success get storage addr: %s, "
                "key: %s, val: %s, valid: %d, thread_idx: %d", 
                view_,
                common::Encode::HexEncode(id).c_str(),
                common::Encode::HexEncode(key_str).c_str(),
                "",
                false,
                thread_idx);
        }
    } else {
        ZJC_DEBUG("addr invalid view: %lu, 0 0 success get storage addr: %s, "
            "key: %s, val: %s, valid: %d, thread_idx: %d", 
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            "",
            false,
            thread_idx);
    }

    // auto str_key = std::string((char*)addr.bytes, sizeof(addr.bytes)) +
    //     std::string((char*)key.bytes, sizeof(key.bytes));
    // auto prev_iter = prev_storages_map_.find(str_key);
    // if (prev_iter != prev_storages_map_.end()) {
    //     evmc::bytes32 tmp_val{};
    //     uint32_t offset = 0;
    //     uint32_t length = sizeof(tmp_val.bytes);
    //     if (prev_iter->second.size() < sizeof(tmp_val.bytes)) {
    //         offset = sizeof(tmp_val.bytes) - prev_iter->second.size();
    //         length = prev_iter->second.size();
    //     }

    //     memcpy(tmp_val.bytes + offset, prev_iter->second.c_str(), length);
    //     ZJC_DEBUG("success get prev storage key: %s, value: %s",
    //         common::Encode::HexEncode(str_key).c_str(),
    //         common::Encode::HexEncode(prev_iter->second).c_str());
    //     return tmp_val;
    // }
    auto res_val = view_block_chain_->GetPrevStorageBytes32KeyValue(parent_hash_, addr, key);
    if (res_val) {
        ZJC_DEBUG("view: %lu,  success get storage addr: %s, key: %s, "
            "val: %s, valid: %d, parent_hash_: %s, thread_idx: %d", 
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            common::Encode::HexEncode(std::string((char*)res_val.bytes, 32)).c_str(),
            true,
            common::Encode::HexEncode(parent_hash_).c_str(),
            thread_idx);
        return res_val;
    }

    evmc::bytes32 tmp_val{};
    auto res_bytes = Execution::Instance()->GetStorage(addr, key, &tmp_val);
    if (!res_bytes) {
        // ZJC_DEBUG("failed get prev storage key: %s", common::Encode::HexEncode(str_key).c_str());
    }

    ZJC_DEBUG("view: %lu, 2 success get storage addr: %s, key: %s, val: %s, valid: %d, thread_idx: %d", 
        view_,
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key_str).c_str(),
        common::Encode::HexEncode(std::string((char*)tmp_val.bytes, 32)).c_str(),
        (tmp_val ? true : false),
        thread_idx);
    return tmp_val;
}

evmc_storage_status ZjchainHost::set_storage(
        const evmc::address& addr,
        const evmc::bytes32& key,
        const evmc::bytes32& value) noexcept {
    // just set temporary map storage, when commit set to db and block
    std::string id((char*)addr.bytes, sizeof(addr.bytes));
    std::string key_str((char*)key.bytes, sizeof(key.bytes));
    std::string val_str((char*)value.bytes, sizeof(value.bytes));
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    ZJC_DEBUG("3_15_%lu, thread_idx: %d, zjcvm set storage called, id: %s, key: %s, value: %s",
        view_,
        thread_idx,
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key_str).c_str(),
        common::Encode::HexEncode(val_str).c_str());
    auto it = accounts_.find(addr);
    if (it == accounts_.end()) {
        accounts_[addr] = MockedAccount();
        CHECK_MEMORY_SIZE(accounts_);
        it = accounts_.find(addr);
    }

    it->second.storage[key] = value;

    // auto storage_iter = it->second.storage.find(key);
    // if (storage_iter != it->second.storage.end()) {
    //     if (storage_iter->second.value == value) {
    //         return EVMC_STORAGE_ADDED;
    //         // return EVMC_STORAGE_MODIFIED_RESTORED;
    //     }
    // }

    // if (storage_iter != it->second.storage.end()) {
    //     storage_iter->second.value = value;
    // } else {
    //     it->second.storage[key] = value;
    //     storage_iter = it->second.storage.find(key);
    // }

    // auto& old = storage_iter->second;
    // evmc_storage_status status{};
    // if (!old.dirty) {
    //     old.dirty = true;
    //     if (!old.value)
    //         status = EVMC_STORAGE_ADDED;
    //     else if (value)
    //         status = EVMC_STORAGE_MODIFIED;
    //     else
    //         status = EVMC_STORAGE_DELETED;
    // } else {
    //     status = EVMC_STORAGE_MODIFIED_RESTORED;
    // }

    return EVMC_STORAGE_ADDED;
}

evmc::uint256be ZjchainHost::get_balance(const evmc::address& addr) const noexcept {
    // don't use real balance
    ZJC_DEBUG("called 3");
    auto iter = account_balance_.find(addr);
    if (iter == account_balance_.end()) {
        ZJC_DEBUG("failed now get balace: %s, my: %s, origin: %s",
            common::Encode::HexEncode(std::string((char*)addr.bytes, 20)).c_str(),
            common::Encode::HexEncode(my_address_).c_str(),
            common::Encode::HexEncode(origin_address_).c_str());
        return {};
    }

    auto val = EvmcBytes32ToUint64(iter->second);
    ZJC_DEBUG("success now get balace: %s, my: %s, origin: %s, %lu",
        common::Encode::HexEncode(std::string((char*)addr.bytes, 20)).c_str(),
        common::Encode::HexEncode(my_address_).c_str(),
        common::Encode::HexEncode(origin_address_).c_str(),
        val);
    return iter->second;
}

size_t ZjchainHost::get_code_size(const evmc::address& addr) const noexcept {
    std::string id = std::string((char*)addr.bytes, sizeof(addr.bytes));
    ZJC_DEBUG("now get contract bytes code size: %s", common::Encode::HexEncode(id).c_str());
    protos::AddressInfoPtr acc_info = acc_mgr_->GetAccountInfo(id);
    if (acc_info == nullptr) {
        ZJC_DEBUG("failed get contract bytes code size: %s", common::Encode::HexEncode(id).c_str());
        assert(false);
        return 0;
    }

    ZJC_DEBUG("success get contract bytes code size: %s, %d",
        common::Encode::HexEncode(id).c_str(), acc_info->bytes_code().size());
    return acc_info->bytes_code().size();
}

evmc::bytes32 ZjchainHost::get_code_hash(const evmc::address& addr) const noexcept {
    assert(false);
    ZJC_DEBUG("called 5");
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
    assert(false);
    ZJC_DEBUG("called 6");
    std::string id = std::string((char*)addr.bytes, sizeof(addr.bytes));
    protos::AddressInfoPtr acc_info = acc_mgr_->GetAccountInfo(id);
    if (acc_info == nullptr) {
        return 0;
    }

    auto& code = acc_info->bytes_code();
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
    ZJC_DEBUG("selfdestruct called addr: %s, beneficiary: %s",
        common::Encode::HexEncode(std::string((char*)addr.bytes, 20)).c_str(),
        common::Encode::HexEncode(std::string((char*)beneficiary.bytes, 20)).c_str());
    if (recorded_selfdestructs_ != nullptr) {
        assert(false);
        return false;
    }

    recorded_selfdestructs_ = std::make_shared<selfdestuct_record>(addr, beneficiary);
    return true;
}

evmc::Result ZjchainHost::call(const evmc_message& msg) noexcept {
    ZJC_DEBUG("called 8");
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
    ZJC_DEBUG("host called kind: %u, from: %s, to: %s, amount: %lu",
        msg.kind, common::Encode::HexEncode(params.from).c_str(), 
        common::Encode::HexEncode(params.to).c_str(), params.value);
    if (contract_mgr_->call(
            params,
            gas_price_,
            origin_address_,
            raw_result) != contract::kContractNotExists) {
        ZJC_DEBUG("call default contract failed: %s", common::Encode::HexEncode(origin_address_).c_str());
    } else {
        std::string id = std::string((char*)msg.code_address.bytes, sizeof(msg.code_address.bytes));
        protos::AddressInfoPtr acc_info = acc_mgr_->GetAccountInfo(id);
        if (acc_info != nullptr) {
            if (!acc_info->bytes_code().empty()) {
                if (acc_info->pool_index() != view_block_chain_->pool_index()) {
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
                if (res_status != consensus::kConsensusSuccess || evmc_res.status_code != EVMC_SUCCESS) {
                    return evmc_res;
                }
            }
        }
    }

	// 交易转账缓存
    if (params.value > 0) {
        uint64_t from_balance = EvmcBytes32ToUint64(get_balance(msg.sender));
        if (from_balance < params.value) {
            evmc_res.status_code = EVMC_INSUFFICIENT_BALANCE;
        } else {
            std::string from_str = std::string((char*)msg.sender.bytes, sizeof(msg.sender.bytes));
            std::string dest_str = std::string((char*)msg.recipient.bytes, sizeof(msg.recipient.bytes));
            auto sender_iter = to_account_value_.find(from_str);
            if (sender_iter == to_account_value_.end()) {
                to_account_value_[from_str] = std::map<std::string, uint64_t>();
                to_account_value_[from_str][dest_str] = params.value;
                CHECK_MEMORY_SIZE(to_account_value_);
                CHECK_MEMORY_SIZE(to_account_value_[from_str]);
            } else {
                auto iter = sender_iter->second.find(dest_str);
                if (iter != sender_iter->second.end()) {
                    sender_iter->second[dest_str] += params.value;
                } else {
                    sender_iter->second[dest_str] = params.value;
                }
            }

            evmc_res.status_code = EVMC_SUCCESS;
            ZJC_DEBUG("contract transfer from: %s, to: %s, from_balance: %lu, amount: %lu",
                common::Encode::HexEncode(from_str).c_str(),
                common::Encode::HexEncode(dest_str).c_str(),
                from_balance,
                params.value);
        }
    }
    
    return evmc_res;
}

evmc_tx_context ZjchainHost::get_tx_context() const noexcept {
    // assert(false);
    ZJC_DEBUG("emit called block number: %lu, block timestamp: %lu", tx_context_.block_number, tx_context_.block_timestamp);
    return tx_context_;
}

evmc::bytes32 ZjchainHost::get_block_hash(int64_t block_number) const noexcept {
    ZJC_DEBUG("called 10");
    assert(false);
    return {};
}

void ZjchainHost::emit_log(const evmc::address& addr,
                const uint8_t* data,
                size_t data_size,
                const evmc::bytes32 topics[],
                size_t topics_count) noexcept {
#ifndef NDEBUG
    std::string topics_str;
    for (uint32_t i = 0; i < topics_count; ++i) {
        topics_str += common::Encode::HexEncode(std::string((char*)topics[i].bytes, sizeof(topics[i].bytes))) + ", ";
    }

    ZJC_WARN("emit_log caller: %s, data: %s, topics: %s",
        common::Encode::HexEncode(std::string((char*)addr.bytes, sizeof(addr.bytes))).c_str(),
        common::Encode::HexEncode(std::string((char*)data, data_size)).c_str(),
        topics_str.c_str());
#endif

    recorded_logs_.push_back({ addr, std::string((char*)data, data_size), {topics, topics + topics_count} });
}

void ZjchainHost::AddTmpAccountBalance(const std::string& address, uint64_t balance) {
    ZJC_DEBUG("called 12");
    evmc::address addr;
    memcpy(
        addr.bytes,
        address.c_str(),
        sizeof(addr.bytes));
    evmc::bytes32 tmp_val{};
    Uint64ToEvmcBytes32(tmp_val, balance);
    account_balance_[addr] = tmp_val;
    CHECK_MEMORY_SIZE(account_balance_);
}

int ZjchainHost::SaveKeyValue(
        const std::string& id,
        const std::string& key,
        const std::string& val) {
    ZJC_DEBUG("called 13");
    auto addr = evmc::address{};
    memcpy(addr.bytes, id.c_str(), id.size());
    CONTRACT_DEBUG("zjcvm set storage called, id: %s, key: %s, value: %s",
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key).c_str(),
        common::Encode::HexEncode(val).c_str());
    return SaveKeyValue(addr, key, val);
}

int ZjchainHost::SaveKeyValue(
        const evmc::address& addr,
        const std::string& key,
        const std::string& val) {
    ZJC_DEBUG("called 13");
    CONTRACT_DEBUG("view: %lu, zjcvm set storage called, id: %s, key: %s, value: %s",
        view_,
        common::Encode::HexEncode(std::string((char*)addr.bytes, sizeof(addr.bytes))).c_str(),
        common::Encode::HexEncode(key).c_str(),
        common::Encode::HexEncode(val).c_str());
    auto it = accounts_.find(addr);
    if (it == accounts_.end()) {
        accounts_[addr] = MockedAccount();
        CHECK_MEMORY_SIZE(accounts_);
        it = accounts_.find(addr);
    }

    auto& old = it->second.str_storage[key];
    old.str_val = val;
    return kZjcvmSuccess;
}

int ZjchainHost::GetCachedKeyValue(
        const std::string& id, 
        const std::string& key_str, 
        std::string* val) {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    CONTRACT_DEBUG("view: %lu, zjcvm get storage called, id: %s, key: %s, value: %s, thread_idx: %d",
        view_,
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key_str).c_str(),
        common::Encode::HexEncode(*val).c_str(),
        thread_idx);

    auto addr = evmc::address{};
    memcpy(addr.bytes, id.c_str(), id.size());
    auto it = accounts_.find(addr);
    if (it != accounts_.end()) {
        auto siter = it->second.str_storage.find(key_str);
        if (siter != it->second.str_storage.end()) {
            *val = siter->second.str_val;
            return kZjcvmSuccess;
        }
        
        CONTRACT_DEBUG("key invalid, view: %lu, zjcvm get storage called, id: %s, key: %s, value: %s, thread_idx: %d",
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            common::Encode::HexEncode(*val).c_str(),
            thread_idx);
    } else {
        CONTRACT_DEBUG("addr invalid, view: %lu, zjcvm get storage called, id: %s, key: %s, value: %s, thread_idx: %d",
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            common::Encode::HexEncode(*val).c_str(),
            thread_idx);
    }
    return kZjcvmError;
}

int ZjchainHost::GetKeyValue(const std::string& id, const std::string& key_str, std::string* val) {
    auto addr = evmc::address{};
    memcpy(addr.bytes, id.c_str(), id.size());
    auto it = accounts_.find(addr);
    if (it != accounts_.end()) {
        auto siter = it->second.str_storage.find(key_str);
        if (siter != it->second.str_storage.end()) {
            *val = siter->second.str_val;
            return kZjcvmSuccess;
        }
    }

    auto str_key = id + key_str;
    if (view_block_chain_->GetPrevStorageKeyValue(parent_hash_, id, key_str, val)) {
        return kZjcvmSuccess;
    }
    // auto prev_iter = prev_storages_map_.find(str_key);
    // if (prev_iter != prev_storages_map_.end()) {
    //     *val = prev_iter->second;
    //     return kZjcvmSuccess;
    // }

    ZJC_DEBUG("called 14");
    if (!Execution::Instance()->GetStorage(addr, key_str, val)) {
        return kZjcvmError;
    }

    return kZjcvmSuccess;
}

evmc_access_status ZjchainHost::access_account(const evmc::address& addr) noexcept {
    ZJC_DEBUG("called 15");
    return EVMC_ACCESS_COLD;
    if (Execution::Instance()->AddressWarm(addr)) {
        return EVMC_ACCESS_WARM;
    }

    return EVMC_ACCESS_COLD;
}

evmc_access_status ZjchainHost::access_storage(
        const evmc::address& addr,
        const evmc::bytes32& key) noexcept {
    ZJC_DEBUG("called 16");
    return EVMC_ACCESS_COLD;
    if (Execution::Instance()->StorageKeyWarm(addr, key)) {
        return EVMC_ACCESS_WARM;
    }

    return EVMC_ACCESS_COLD;
}
}  // namespace zjcvm

}  // namespace shardora
