#include "shardoravm/shardora_host.h"

#include <evmc/hex.hpp>

#include "block/account_manager.h"
#include "common/encode.h"
#include "common/log.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "contract/call_parameters.h"
#include "contract/contract_manager.h"
#include "protos/prefix_db.h"
#include "shardoravm/execution.h"
#include "shardoravm/shardoravm_utils.h"

namespace shardora {

namespace shardoravm {

namespace {

std::string SafeEvmcOutput(const evmc_result& res) {
    if (res.output_data == nullptr || res.output_size == 0) {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(res.output_data), res.output_size);
}

}  // namespace

bool ShardorahainHost::account_exists(const evmc::address& addr) const noexcept {
    SHARDORA_DEBUG("called 0");
    std::string addr_str((char*)addr.bytes, sizeof(addr.bytes));
    if (!view_block_chain_) {
        SHARDORA_DEBUG("account_exists: view_block_chain_ is null, addr: %s",
            common::Encode::HexEncode(addr_str).c_str());
        return false;
    }

    auto address_info = view_block_chain_->ChainGetAccountInfo(addr_str);
    if (address_info != nullptr) {
        SHARDORA_DEBUG("account_exists: found addr: %s",
            common::Encode::HexEncode(addr_str).c_str());
        return true;
    }

    SHARDORA_DEBUG("account_exists: not found addr: %s",
        common::Encode::HexEncode(addr_str).c_str());
    return false;
}

evmc::bytes32 ShardorahainHost::GetCachedStorage(
        const evmc::address& addr,
        const evmc::bytes32& key) const noexcept {
    auto it = accounts_.find(addr);
    if (it != accounts_.end()) {
        auto storage_iter = it->second.storage.find(key);
        if (storage_iter != it->second.storage.end()) {
            return storage_iter->second.value;
        }
    }

    evmc::bytes32 tmp_val{};
    return tmp_val;
}

evmc::bytes32 ShardorahainHost::get_storage(
        const evmc::address& addr,
        const evmc::bytes32& key) const noexcept {
    // Fast path: check in-transaction cache first
    auto it = accounts_.find(addr);
    if (it != accounts_.end()) {
        auto storage_iter = it->second.storage.find(key);
        if (storage_iter != it->second.storage.end()) {
            return storage_iter->second.value;
        }
    }

    // Check parent transaction's cache (same block, previous tx)
    if (pre_shardora_host_ != nullptr) {
        auto parent_val = pre_shardora_host_->get_storage(addr, key);
        if (parent_val) {
            // Cache for subsequent reads
            const_cast<ShardorahainHost*>(this)->accounts_[addr].storage[key] = {parent_val};
            return parent_val;
        }
        return parent_val;
    }

    // Check view block chain (uncommitted blocks)
    if (view_block_chain_) {
        auto res_val = view_block_chain_->GetPrevStorageBytes32KeyValue(parent_hash_, addr, key);
        if (res_val) {
            const_cast<ShardorahainHost*>(this)->accounts_[addr].storage[key] = {res_val};
            return res_val;
        }
    }

    // Final fallback: read from DB
    evmc::bytes32 tmp_val{};
    Execution::Instance()->GetStorage(addr, key, &tmp_val);
    if (tmp_val) {
        const_cast<ShardorahainHost*>(this)->accounts_[addr].storage[key] = {tmp_val};
    }
    return tmp_val;
}

evmc_storage_status ShardorahainHost::set_storage(
        const evmc::address& addr,
        const evmc::bytes32& key,
        const evmc::bytes32& value) noexcept {
    auto it = accounts_.find(addr);
    if (it == accounts_.end()) {
        accounts_[addr] = MockedAccount();
        it = accounts_.find(addr);
    }

    auto& old = it->second.storage[key];
    if (!old.dirty) {
        gas_more_ += consensus::kSstoreNewSlotGas;
    } else {
        gas_more_ += consensus::kSstoreDirtySlotGas;
    }

    old.value = value;
    old.dirty = true;
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

    contract_to_call_dirty_ = true;
    return EVMC_STORAGE_ADDED;
}

evmc::uint256be ShardorahainHost::get_balance(const evmc::address& addr) const noexcept {
    auto iter = account_balance_.find(addr);
    if (iter != account_balance_.end()) {
        return iter->second;
    }

    if (pre_shardora_host_ != nullptr) {
        return pre_shardora_host_->get_balance(addr);
    }
    
    if (!view_block_chain_) return {};
    auto acc_info = view_block_chain_->ChainGetAccountInfo(
        std::string((char*)addr.bytes, sizeof(addr.bytes)));
    if (acc_info == nullptr) {
        return {};
    }

    evmc::uint256be res_val;
    Uint64ToEvmcBytes32(res_val, acc_info->balance());
    return res_val;
}

size_t ShardorahainHost::get_code_size(const evmc::address& addr) const noexcept {
    std::string id = std::string((char*)addr.bytes, sizeof(addr.bytes));
    auto pre_addr = common::Encode::HexDecode("00000000000000000000000000000000000000");
    if (memcmp(id.c_str(), pre_addr.c_str(), pre_addr.size()) == 0) {
        return 1;
    }

    SHARDORA_DEBUG("now get contract bytes code size: %s", common::Encode::HexEncode(id).c_str());
    if (!view_block_chain_) {
        SHARDORA_DEBUG("get_code_size: view_block_chain_ is null, addr: %s",
            common::Encode::HexEncode(id).c_str());
        return 0;
    }

    protos::AddressInfoPtr acc_info = view_block_chain_->ChainGetAccountInfo(id);
    if (acc_info == nullptr) {
        auto iter = create2_accounts_.find(addr);
        if (iter != create2_accounts_.end()) {
            return iter->second.code.size();
        }

        SHARDORA_ERROR("failed get contract bytes code size: %s", common::Encode::HexEncode(id).c_str());
        // //assert(false);
        return 0;
    }

    SHARDORA_DEBUG("success get contract bytes code size: %s, %d",
        common::Encode::HexEncode(id).c_str(), acc_info->bytes_code().size());
    return acc_info->bytes_code().size();
}

evmc::bytes32 ShardorahainHost::get_code_hash(const evmc::address& addr) const noexcept {
    SHARDORA_DEBUG("called 5");
    std::string id = std::string((char*)addr.bytes, sizeof(addr.bytes));

    // 1. Check in-flight create2 accounts first
    auto create2_iter = create2_accounts_.find(addr);
    if (create2_iter != create2_accounts_.end()) {
        const auto& code = create2_iter->second.code;
        if (code.empty()) {
            return {};
        }
        std::string code_str(code.begin(), code.end());
        std::string code_hash = common::Hash::keccak256(code_str);
        evmc::bytes32 tmp_val{};
        memcpy(tmp_val.bytes, code_hash.c_str(), sizeof(tmp_val.bytes));
        return tmp_val;
    }

    // 2. Look up committed account info via chain
    if (!view_block_chain_) {
        SHARDORA_DEBUG("get_code_hash: view_block_chain_ is null, addr: %s",
            common::Encode::HexEncode(id).c_str());
        return {};
    }

    protos::AddressInfoPtr acc_info = view_block_chain_->ChainGetAccountInfo(id);
    if (acc_info == nullptr || acc_info->bytes_code().empty()) {
        SHARDORA_DEBUG("get_code_hash: no code for addr: %s", common::Encode::HexEncode(id).c_str());
        return {};
    }

    std::string code_hash = common::Hash::keccak256(acc_info->bytes_code());
    evmc::bytes32 tmp_val{};
    memcpy(tmp_val.bytes, code_hash.c_str(), sizeof(tmp_val.bytes));
    SHARDORA_DEBUG("get_code_hash addr: %s, hash: %s",
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(code_hash).c_str());
    return tmp_val;
}

size_t ShardorahainHost::copy_code(
        const evmc::address& addr,
        size_t code_offset,
        uint8_t* buffer_data,
        size_t buffer_size) const noexcept {
    SHARDORA_DEBUG("called 6");
    std::string id = std::string((char*)addr.bytes, sizeof(addr.bytes));

    // 1. Check in-flight create2 accounts first
    auto create2_iter = create2_accounts_.find(addr);
    if (create2_iter != create2_accounts_.end()) {
        const auto& code = create2_iter->second.code;
        if (code_offset >= code.size()) {
            return 0;
        }
        const auto n = (std::min)(buffer_size, code.size() - code_offset);
        if (n > 0) {
            std::copy_n(code.data() + code_offset, n, buffer_data);
        }
        return n;
    }

    // 2. Look up committed account info via chain
    if (!view_block_chain_) {
        SHARDORA_DEBUG("copy_code: view_block_chain_ is null, addr: %s",
            common::Encode::HexEncode(id).c_str());
        return 0;
    }

    protos::AddressInfoPtr acc_info = view_block_chain_->ChainGetAccountInfo(id);
    if (acc_info == nullptr) {
        SHARDORA_DEBUG("copy_code: no account for addr: %s", common::Encode::HexEncode(id).c_str());
        return 0;
    }

    auto& code = acc_info->bytes_code();
    if (code_offset >= code.size()) {
        return 0;
    }

    const auto n = (std::min)(buffer_size, code.size() - code_offset);
    if (n > 0) {
        std::copy_n(reinterpret_cast<const uint8_t*>(code.data()) + code_offset, n, buffer_data);
    }

    SHARDORA_DEBUG("copy_code addr: %s, offset: %zu, copied: %zu",
        common::Encode::HexEncode(id).c_str(), code_offset, n);
    return n;
}

bool ShardorahainHost::selfdestruct(
        const evmc::address& addr,
        const evmc::address& beneficiary) noexcept {
    contract_to_call_dirty_ = true;
    SHARDORA_DEBUG("selfdestruct called addr: %s, beneficiary: %s",
        common::Encode::HexEncode(std::string((char*)addr.bytes, 20)).c_str(),
        common::Encode::HexEncode(std::string((char*)beneficiary.bytes, 20)).c_str());
    if (recorded_selfdestructs_ != nullptr) {
        //assert(false);
        return false;
    }

    recorded_selfdestructs_ = std::make_shared<selfdestuct_record>(addr, beneficiary);
    return true;
}

evmc::Result ShardorahainHost::call(const evmc_message& msg) noexcept {
    SHARDORA_DEBUG("called 8");
    contract::CallParameters params;
    params.shardora_host = this;
    params.gas = msg.gas;
    params.apparent_value = shardoravm::EvmcBytes32ToUint64(msg.value);
    params.value = (msg.kind == EVMC_DELEGATECALL) ? 0 : params.apparent_value;
    auto address_to_str = [](const evmc_address& addr) {
        return std::string(reinterpret_cast<const char*>(addr.bytes), sizeof(addr.bytes));
    };

    params.from = address_to_str(msg.sender);
    params.code_address = address_to_str(msg.code_address);
    if (msg.kind == EVMC_DELEGATECALL || msg.kind == EVMC_CALLCODE) {
        params.to = my_address_;
    } else {
        params.to = address_to_str(msg.recipient);
    }

    // params.to = msg.kind == EVMC_CALL ? params.code_address : my_address_;
    params.data = std::string((char*)msg.input_data, msg.input_size);
    params.on_op = {};
    params.create2_salt = msg.create2_salt;
    evmc_result call_result = {};
    evmc::Result evmc_res{ call_result };
    evmc_result* raw_result = (evmc_result*)&evmc_res;
    raw_result->gas_left = msg.gas;
    SHARDORA_DEBUG("host called kind: %u, from: %s, to: %s, amount: %lu, gas limit: %lu, input_data: %s, salt: %lu, code: %s",
        (int32_t)msg.kind, common::Encode::HexEncode(params.from).c_str(), 
        common::Encode::HexEncode(params.to).c_str(), params.value, params.gas,
        common::Encode::HexEncode(params.data).c_str(),
        EvmcBytes32ToUint64(msg.create2_salt),
        "");

    int call_res = contract_mgr_->call(
            params,
            gas_price_,
            origin_address_,
            raw_result);
    if (call_res != contract::kContractNotExists) {
        if (call_res == contract::kContractSuccess && params.code_address == contract::kContractCreate2) {
            if (raw_result->output_data == nullptr || raw_result->output_size < 32) {
                evmc_res.status_code = EVMC_FAILURE;
                SHARDORA_ERROR("create2 precompile returned invalid output, output_size: %zu, status: %d",
                    raw_result->output_size, (int32_t)raw_result->status_code);
                return evmc_res;
            }

            std::string id((char*)raw_result->output_data + 12, 20);
            auto params2 = params;
            params2.to = id;
            SHARDORA_DEBUG("get call bytes code success: %s, field: %s, value: %s",
                common::Encode::HexEncode(id).c_str(),
                protos::kFieldBytesCode.c_str(),
                common::Encode::HexEncode(params2.data).c_str());
            evmc_result call_result2 = {};
            evmc::Result evmc_res2{ call_result2 };
            evmc_result* raw_result2 = (evmc_result*)&evmc_res2;
            int res_status = shardoravm::Execution::Instance()->execute(
                params2.data,
                "",
                params2.from,
                params2.to,
                origin_address_,
                params2.apparent_value,
                params2.gas,
                static_cast<uint32_t>(msg.depth),
                shardoravm::kCreate2,
                *this,
                &evmc_res2);
            evmc_res.gas_left = evmc_res2.gas_left;
            if (res_status != consensus::kConsensusSuccess || evmc_res2.status_code != EVMC_SUCCESS) {
                return evmc_res2;
            }

            // Transaction transfer cache
            if (params2.value > 0) {
                uint64_t from_balance = EvmcBytes32ToUint64(get_balance(params2.from));
                if (from_balance < params2.value) {
                    evmc_res.status_code = EVMC_INSUFFICIENT_BALANCE;
                } else {
                    if (params2.from == params2.to) {
                        evmc_res.status_code = EVMC_INSUFFICIENT_BALANCE;
                        return evmc_res;
                    }

                    SHARDORA_DEBUG("craete2 contract transfer from: %s, to: %s, from_balance: %lu, amount: %lu",
                        common::Encode::HexEncode(params2.from).c_str(),
                        common::Encode::HexEncode(params2.to).c_str(),
                        from_balance,
                        params2.value);
                }
            }

            AddCreate2Contract(id, SafeEvmcOutput(evmc_res2.raw()), params2.value);
        }
        SHARDORA_DEBUG("call default contract failed: %s", common::Encode::HexEncode(origin_address_).c_str());
    } else {
        std::string id = params.code_address;
        if (!view_block_chain_) {
            evmc_res.status_code = EVMC_FAILURE;
            SHARDORA_ERROR("host call failed: view_block_chain_ is null, code_address: %s",
                common::Encode::HexEncode(id).c_str());
            return evmc_res;
        }

        protos::AddressInfoPtr acc_info = view_block_chain_->ChainGetAccountInfo(id);
        if (acc_info != nullptr) {
            if (!acc_info->bytes_code().empty()) {
                SHARDORA_DEBUG("get call bytes code success: %s, field: %s, value: %s",
                    common::Encode::HexEncode(id).c_str(),
                    protos::kFieldBytesCode.c_str(),
                    common::Encode::HexEncode(acc_info->bytes_code()).c_str());
                contract_to_call_dirty_ = false;
                int res_status = shardoravm::Execution::Instance()->execute(
                    acc_info->bytes_code(),
                    params.data,
                    params.from,
                    params.to,
                    origin_address_,
                    params.apparent_value,
                    params.gas,
                    static_cast<uint32_t>(msg.depth),
                    shardoravm::kJustCall,
                    *this,
                    &evmc_res);
                if (acc_info->pool_index() == view_block_chain_->pool_index()) {
                    contract_to_call_dirty_ = false;
                }
                
                if (contract_to_call_dirty_) {
                    evmc_res.status_code = EVMC_REVERT;
                    SHARDORA_DEBUG("contract to call contract should not modify status. not support: %s, %s",
                        common::Encode::HexEncode(id).c_str(),
                        protos::kFieldBytesCode.c_str());
                    return evmc_res;
                }

                if (res_status != consensus::kConsensusSuccess || evmc_res.status_code != EVMC_SUCCESS) {
                    return evmc_res;
                }
            }
        }
    }

	// Transaction transfer cache
    if (params.value > 0) {
        uint64_t from_balance = EvmcBytes32ToUint64(get_balance(msg.sender));
        if (from_balance < params.value) {
            evmc_res.status_code = EVMC_INSUFFICIENT_BALANCE;
        } else {
            std::string from_str = std::string((char*)msg.sender.bytes, sizeof(msg.sender.bytes));
            std::string dest_str = std::string((char*)msg.recipient.bytes, sizeof(msg.recipient.bytes));
            if (from_str == dest_str) {
                evmc_res.status_code = EVMC_INSUFFICIENT_BALANCE;
                return evmc_res;
            }

            auto sender_iter = to_account_value_.find(from_str);
            if (sender_iter == to_account_value_.end()) {
                to_account_value_[from_str] = std::map<std::string, uint64_t>();
                to_account_value_[from_str][dest_str] = params.value;
            } else {
                auto iter = sender_iter->second.find(dest_str);
                if (iter != sender_iter->second.end()) {
                    sender_iter->second[dest_str] += params.value;
                } else {
                    sender_iter->second[dest_str] = params.value;
                }
            }

            evmc_res.status_code = EVMC_SUCCESS;
            SHARDORA_DEBUG("contract transfer from: %s, to: %s, from_balance: %lu, amount: %lu",
                common::Encode::HexEncode(from_str).c_str(),
                common::Encode::HexEncode(dest_str).c_str(),
                from_balance,
                params.value);
        }
    }
    
    SHARDORA_DEBUG("success host call kind: %u, from: %s, to: %s, amount: %lu, "
        "gas limit: %lu, gas left: %lu, status: %d, outdata: %s, input: %s",
        (int32_t)msg.kind, common::Encode::HexEncode(params.from).c_str(), 
        common::Encode::HexEncode(params.to).c_str(), params.value, params.gas, 
        evmc_res.gas_left, (int32_t)evmc_res.status_code,
        common::Encode::HexEncode(SafeEvmcOutput(evmc_res.raw())).c_str(),
        common::Encode::HexEncode(params.data).c_str());
    return evmc_res;
}

evmc_tx_context ShardorahainHost::get_tx_context() const noexcept {
    // //assert(false);
    SHARDORA_DEBUG("emit called block number: %lu, block timestamp: %lu, gas: %lu",
        tx_context_.block_number,
        tx_context_.block_timestamp, 
        tx_context_.block_gas_limit);
    return tx_context_;
}

evmc::bytes32 ShardorahainHost::get_block_hash(int64_t block_number) const noexcept {
    SHARDORA_DEBUG("called 10, block_number: %ld", block_number);
    return {};
    
    // EVM BLOCKHASH opcode: returns hash of one of the 256 most recent complete blocks.
    // block_number must be in range [current_block - 256, current_block).
    int64_t current_block = static_cast<int64_t>(tx_context_.block_number);
    
    if (block_number >= current_block || block_number < 0) {
        SHARDORA_DEBUG("get_block_hash: block_number %ld out of range (current: %ld)", 
            block_number, current_block);
        return {};
    }
    
    if (current_block - block_number > 256) {
        SHARDORA_DEBUG("get_block_hash: block_number %ld too old (current: %ld)", 
            block_number, current_block);
        return {};
    }

    // Use view_block_chain to get the block at the requested height.
    // We need network_id and pool_index — get from view_block_chain.
    uint32_t pool_idx = view_block_chain_->pool_index();
    
    // For network_id, we need to get it from the chain's committed blocks.
    // The LatestCommittedBlock should have the network_id.
    auto latest_block = view_block_chain_->LatestCommittedBlock();
    if (!latest_block) {
        SHARDORA_DEBUG("get_block_hash: no latest committed block");
        return {};
    }
    
    uint32_t network_id = latest_block->qc().network_id();
    uint64_t height = static_cast<uint64_t>(block_number);
    
    auto view_block = view_block_chain_->GetWithHeight(network_id, height);
    if (!view_block || view_block->qc().view_block_hash().empty()) {
        SHARDORA_DEBUG("get_block_hash: no block at height %lu for network %u pool %u", 
            height, network_id, pool_idx);
        return {};
    }

    const std::string& block_hash = view_block->qc().view_block_hash();
    evmc::bytes32 result{};
    size_t copy_len = (std::min)(sizeof(result.bytes), block_hash.size());
    memcpy(result.bytes, block_hash.data(), copy_len);
    
    SHARDORA_DEBUG("get_block_hash: block_number %ld -> hash %s", 
        block_number, common::Encode::HexEncode(block_hash).c_str());
    return result;
}

void ShardorahainHost::emit_log(const evmc::address& addr,
                const uint8_t* data,
                size_t data_size,
                const evmc::bytes32 topics[],
                size_t topics_count) noexcept {
#ifndef NDEBUG
    std::string topics_str;
    for (uint32_t i = 0; i < topics_count; ++i) {
        topics_str += common::Encode::HexEncode(std::string((char*)topics[i].bytes, sizeof(topics[i].bytes))) + ", ";
    }

    SHARDORA_DEBUG("emit_log caller: %s, data: %s, topics: %s",
        common::Encode::HexEncode(std::string((char*)addr.bytes, sizeof(addr.bytes))).c_str(),
        common::Encode::HexEncode(std::string((char*)data, data_size)).c_str(),
        topics_str.c_str());
#endif

    contract_to_call_dirty_ = true;
    recorded_logs_.push_back({ addr, std::string((char*)data, data_size), {topics, topics + topics_count} });
}

void ShardorahainHost::AddTmpAccountBalance(const std::string& address, uint64_t balance) {
    SHARDORA_DEBUG("called 12");
    evmc::address addr;
    memcpy(
        addr.bytes,
        address.c_str(),
        sizeof(addr.bytes));
    evmc::bytes32 tmp_val{};
    Uint64ToEvmcBytes32(tmp_val, balance);
    account_balance_[addr] = tmp_val;
    contract_to_call_dirty_ = true;
}

int ShardorahainHost::SaveKeyValue(
        const std::string& id,
        const std::string& key,
        const std::string& val) {
    SHARDORA_DEBUG("called 13");
    auto addr = evmc::address{};
    memcpy(addr.bytes, id.c_str(), id.size());
    CONTRACT_DEBUG("shardoravm set storage called, id: %s, key: %s, value: %s",
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key).c_str(),
        common::Encode::HexEncode(val).c_str());
    contract_to_call_dirty_ = true;
    return SaveKeyValue(addr, key, val);
}

int ShardorahainHost::SaveKeyValue(
        const evmc::address& addr,
        const std::string& key,
        const std::string& val) {
    SHARDORA_DEBUG("called 13");
    SHARDORA_DEBUG("view: %lu, shardoravm set storage called, id: %s, key: %s, value: %s",
        view_,
        common::Encode::HexEncode(std::string((char*)addr.bytes, sizeof(addr.bytes))).c_str(),
        common::Encode::HexEncode(key).c_str(),
        common::Encode::HexEncode(val).c_str());
    auto it = accounts_.find(addr);
    if (it == accounts_.end()) {
        accounts_[addr] = MockedAccount();
        it = accounts_.find(addr);
    }

    auto& old = it->second.str_storage[key];
    if (old.dirty) {
        // Update existing slot: charge for the incremental slots added (EIP-2200 dirty rate)
        if (val.size() > old.str_val.size()) {
            size_t extra = val.size() - old.str_val.size();
            uint64_t slots = (static_cast<uint64_t>(extra) + consensus::kStorageSlotBytes - 1)
                             / consensus::kStorageSlotBytes;
            gas_more_ += slots * consensus::kSstoreDirtySlotGas;
        }
    } else {
        // New write: charge for all key+value bytes rounded up to 32-byte slots
        gas_more_ += consensus::CalcKvStorageGas(key.size() + sizeof(addr.bytes), val.size(), true);
    }

    old.dirty = true;
    old.str_val = val;
    contract_to_call_dirty_ = true;
    return kShardoravmSuccess;
}

int ShardorahainHost::GetCachedKeyValue(
        const std::string& id, 
        const std::string& key_str, 
        std::string* val) {
    auto thread_idx = -1;//common::GlobalInfo::Instance()->get_thread_index();
    SHARDORA_DEBUG("view: %lu, shardoravm get storage called, id: %s, key: %s, value: %s, thread_idx: %d",
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
            return kShardoravmSuccess;
        }
        
        CONTRACT_DEBUG("key invalid, view: %lu, shardoravm get storage called, id: %s, key: %s, value: %s, thread_idx: %d",
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            common::Encode::HexEncode(*val).c_str(),
            thread_idx);
    } else {
        CONTRACT_DEBUG("addr invalid, view: %lu, shardoravm get storage called, id: %s, key: %s, value: %s, thread_idx: %d",
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            common::Encode::HexEncode(*val).c_str(),
            thread_idx);
    }
    return kShardoravmError;
}

int ShardorahainHost::GetKeyValue(const std::string& id, const std::string& key_str, std::string* val) {
    auto addr = evmc::address{};
    memcpy(addr.bytes, id.c_str(), id.size());
    auto it = accounts_.find(addr);
    if (it != accounts_.end()) {
        auto siter = it->second.str_storage.find(key_str);
        if (siter != it->second.str_storage.end()) {
            *val = siter->second.str_val;
            SHARDORA_DEBUG("view: %lu, success shardoravm get storage called, id: %s, key: %s, value: %s",
                view_,
                common::Encode::HexEncode(id).c_str(),
                common::Encode::HexEncode(key_str).c_str(),
                common::Encode::HexEncode(*val).c_str());
            return kShardoravmSuccess;
        }
    }

    if (pre_shardora_host_ != nullptr) {
        return pre_shardora_host_->GetKeyValue(id, key_str, val);
    }

    auto str_key = id + key_str;
    if (view_block_chain_ && view_block_chain_->GetPrevStorageKeyValue(parent_hash_, id, key_str, val)) {
        SHARDORA_DEBUG("view: %lu, success shardoravm get storage called, id: %s, key: %s, value: %s",
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            common::Encode::HexEncode(*val).c_str());
        return kShardoravmSuccess;
    }
    // auto prev_iter = prev_storages_map_.find(str_key);
    // if (prev_iter != prev_storages_map_.end()) {
    //     *val = prev_iter->second;
    //     return kShardoravmSuccess;
    // }

    SHARDORA_DEBUG("called 14");
    if (!Execution::Instance()->GetStorage(addr, key_str, val)) {
        SHARDORA_DEBUG("view: %lu, failed shardoravm get storage called, id: %s, key: %s, value: %s",
            view_,
            common::Encode::HexEncode(id).c_str(),
            common::Encode::HexEncode(key_str).c_str(),
            common::Encode::HexEncode(*val).c_str());
        return kShardoravmError;
    }

    SHARDORA_DEBUG("view: %lu, success shardoravm get storage called, id: %s, key: %s, value: %s",
        view_,
        common::Encode::HexEncode(id).c_str(),
        common::Encode::HexEncode(key_str).c_str(),
        "common::Encode::HexEncode(*val).c_str()");
    return kShardoravmSuccess;
}

evmc_access_status ShardorahainHost::access_account(const evmc::address& addr) noexcept {
    SHARDORA_DEBUG("called 15");
    return EVMC_ACCESS_COLD;
    if (Execution::Instance()->AddressWarm(addr)) {
        return EVMC_ACCESS_WARM;
    }

    return EVMC_ACCESS_COLD;
}

evmc_access_status ShardorahainHost::access_storage(
        const evmc::address& addr,
        const evmc::bytes32& key) noexcept {
    SHARDORA_DEBUG("called 16");
    return EVMC_ACCESS_COLD;
    if (Execution::Instance()->StorageKeyWarm(addr, key)) {
        return EVMC_ACCESS_WARM;
    }

    return EVMC_ACCESS_COLD;
}
}  // namespace shardoravm

}  // namespace shardora
