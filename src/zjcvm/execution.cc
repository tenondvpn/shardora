#include "zjcvm/execution.h"

#include "block/account_manager.h"
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

namespace shardora {

namespace zjcvm {

Execution::Execution() {}

Execution::~Execution() {
}

Execution* Execution::Instance() {
    static Execution ins;
    return &ins;
}

void Execution::Init(std::shared_ptr<db::Db>& db, std::shared_ptr<block::AccountManager>& acc_mgr) {
    db_ = db;
    acc_mgr_ = acc_mgr;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    evm_ = evmc::VM{ evmc_create_evmone()};

// 	evmc_loader_error_code ec = EVMC_LOADER_UNSPECIFIED_ERROR;
//     evm_ = evmc::VM{ evmc_load_and_configure("/root/zjchain/third_party/evmone/build/lib64/libevmone.so", &ec)};
// 	if (ec != EVMC_LOADER_SUCCESS) {
// 		const auto error = evmc_last_error_msg();
// 		if (error != nullptr)
// 			std::cerr << error << "\n";
// 		else
// 			std::cerr << "Loading error " << ec << "\n";
//         ZJC_FATAL("evm.set_option error.");
//         return;
// 	}
// 
//     if (evm_.set_option("trace", "0") != EVMC_SET_OPTION_SUCCESS) {
//         ZJC_FATAL("evm.set_option error.");
//         return;
//     }

    // storage_map_ = new common::LimitHashMap<std::string, std::string, 1024>[common::kMaxThreadCount];
}

bool Execution::IsAddressExists(const std::string& addr) {
    protos::AddressInfoPtr address_info = acc_mgr_->GetAccountInfo(addr);
    if (address_info != nullptr) {
        return true;
    }

    return false;
}

bool Execution::AddressWarm(const evmc::address& addr) {
    return false;
}

bool Execution::StorageKeyWarm(
        const evmc::address& addr,
        const evmc::bytes32& key) {
    return false;
}

void Execution::NewBlockWithTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    if (tx.step() != pools::protobuf::kContractCreate &&
            tx.step() != pools::protobuf::kContractExcute &&
            tx.step() != pools::protobuf::kContractCreateByRootTo) {
        return;
    }

    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kCreateContractBytesCode) {
            continue;
        }

        UpdateStorage(tx.storages(i).key(), tx.storages(i).value(), db_batch);
        ZJC_DEBUG("UpdateStoredToDbView %u_%u_%lu, update storage: %s, %s", 
            view_block.qc().network_id(),
            view_block.qc().pool_index(),
            view_block.qc().view(),
            common::Encode::HexEncode(tx.storages(i).key()).c_str(), 
            common::Encode::HexEncode(tx.storages(i).value()).c_str());
    }
}

void Execution::UpdateStorage(
        const std::string& key,
        const std::string& val,
        db::DbWriteBatch& db_batch) {
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    storage_map_[thread_idx].insert(key, val);
    prefix_db_->SaveTemporaryKv(key, val, db_batch);
}

bool Execution::GetStorage(
        const evmc::address& addr,
        const evmc::bytes32& key,
        evmc::bytes32* res_val) {
    auto str_key = std::string((char*)addr.bytes, sizeof(addr.bytes)) +
        std::string((char*)key.bytes, sizeof(key.bytes));
    std::string val;
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    auto thread_count = common::GlobalInfo::Instance()->message_handler_thread_count() - 1;
    if (thread_idx >= thread_count) {
        prefix_db_->GetTemporaryKv(str_key, &val);
    } else {
        prefix_db_->GetTemporaryKv(str_key, &val);
        // if (!storage_map_[thread_idx].Get(str_key, &val)) {
        //     // get from db and add to memory cache
        //     if (prefix_db_->GetTemporaryKv(str_key, &val)) {
        //         storage_map_[thread_idx].Insert(str_key, val);
        //     }
        // }
    }

    ZJC_DEBUG("get storage: %s, %s, valid: %d",
        common::Encode::HexEncode(str_key).c_str(), 
        common::Encode::HexEncode(val).c_str(),
        !val.empty());
    if (val.empty()) {
        return false;
    }

    uint32_t offset = 0;
    uint32_t length = sizeof(res_val->bytes);
    if (val.size() < sizeof(res_val->bytes)) {
        offset = sizeof(res_val->bytes) - val.size();
        length = val.size();
    }

    memcpy(res_val->bytes + offset, val.c_str(), length);
    return true;
}

bool Execution::GetStorage(
        const evmc::address& addr,
        const std::string& key,
        std::string* val) {
    auto str_id = std::string((char*)addr.bytes, sizeof(addr.bytes));
    return GetStorage(str_id, key, val);
}

bool Execution::GetStorage(
        const std::string& str_id,
        const std::string& key,
        std::string* val) {
    auto str_key = str_id + key;
    auto res = true;
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    auto thread_count = common::GlobalInfo::Instance()->message_handler_thread_count() - 1;
    if (thread_idx >= thread_count) {
        prefix_db_->GetTemporaryKv(str_key, val);
    } else {
        prefix_db_->GetTemporaryKv(str_key, val);
        // if (!storage_map_[thread_idx].Get(str_key, val)) {
        //     // get from db and add to memory cache
        //     res = prefix_db_->GetTemporaryKv(str_key, val);
        //     if (res) {
        //         storage_map_[thread_idx].Insert(str_key, *val);
        //     }
        // }
    }

    ZJC_DEBUG("get storage: %s, %s", common::Encode::HexEncode(str_key).c_str(), common::Encode::HexEncode(*val).c_str());
    return res;
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
    auto btime = common::TimeUtils::TimestampMs();
    const size_t code_size = bytes_code.size();
    if (code_size <= kContractHead.size() ||
            from_address.size() != security::kUnicastAddressLength ||
            to_address.size() != security::kUnicastAddressLength ||
            depth >= kContractCallMaxDepth) {
        ZJC_DEBUG("invalid params code_size: %u, from size: %u, "
            "to size: %u, depth: %u, gas_limit: %lu",
            code_size, from_address.size(), to_address.size(), depth, gas_limit);
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
    ZJC_DEBUG("now call contract, msg sender: %s, mode: %d, from: %s, to: %s, value: %lu, bytes_code.size: %ld, input: %s",
        common::Encode::HexEncode(std::string((char*)msg.sender.bytes, 20)).c_str(),
        call_mode,
        common::Encode::HexEncode(from_address).c_str(),
        common::Encode::HexEncode(to_address).c_str(),
        value,
        bytes_code.size(),
        common::Encode::HexEncode(str_input).c_str());
    if (call_mode == kJustCreate || call_mode == kCreateAndCall) {
        msg.kind = EVMC_CREATE;
        *out_res = evm_.execute(
            host,
            rev,
            msg,
            (uint8_t*)bytes_code.c_str(),
            bytes_code.size());
        if (out_res->status_code != EVMC_SUCCESS) {
            const auto gas_used = msg.gas - out_res->gas_left;
            ZJC_ERROR("out_res->status_code != EVMC_SUCCESS.nResult: %d, EVMC_SUCCESS: %d, "
                "gas_used: %lu, gas limit: %lu, codes: %s, from: %s, to: %s",
                out_res->status_code, EVMC_SUCCESS, gas_used, create_gas,
                "common::Encode::HexEncode(bytes_code).c_str()",
                common::Encode::HexEncode(from_address).c_str(),
                common::Encode::HexEncode(to_address).c_str());
            return kZjcvmSuccess;
        } else {
            const auto gas_used = msg.gas - out_res->gas_left;
            ZJC_DEBUG("out_res->status_code != EVMC_SUCCESS.nResult: %d, gas_used: %lu, gas limit: %lu, codes: %s",
                out_res->status_code, gas_used, create_gas, "common::Encode::HexEncode(bytes_code).c_str()");
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

    auto src_gas_left = out_res->gas_left;
    *out_res = evm_.execute(host, rev, msg, exec_code_data, exec_code_size);
    auto etime = common::TimeUtils::TimestampMs();
    ZJC_DEBUG("execute res: %d, from: %s, to: %s, gas_limit: %lu, "
        "src_gas_left: %lu, gas_left: %lu, gas_refund: %lu, use time: %lu",
        out_res->status_code, 
        common::Encode::HexEncode(from_address).c_str(),
        common::Encode::HexEncode(to_address).c_str(),
        src_gas_left,
        gas, out_res->gas_left, out_res->gas_refund, (etime - btime));
    return kZjcvmSuccess;
}

}  // namespace zjcvm

}  //namespace shardora
