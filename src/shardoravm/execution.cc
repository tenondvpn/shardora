#include "shardoravm/execution.h"

#include "block/account_manager.h"
#include "common/encode.h"
#include "common/global_info.h"
#include "evmone/evmone.h"
#include "evmc/loader.h"
#include "evmc/hex.hpp"
#include "evmc/evmc.h"
#include "evmc/mocked_host.hpp"
#include "security/security_utils.h"
#include "shardoravm/shardora_host.h"
#include "shardoravm/shardoravm_utils.h"

namespace shardora {

namespace shardoravm {

namespace {

evmc::VM& ThreadLocalEvm() {
    // evmone::VM reuses ExecutionState by call depth, so keep one VM per execution thread.
    thread_local evmc::VM evm{ evmc_create_evmone() };
    return evm;
}

std::string SafeEvmcOutput(const evmc::Result& res) {
    if (res.output_data == nullptr || res.output_size == 0) {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(res.output_data), res.output_size);
}

}  // namespace

Execution::Execution() {}

Execution::~Execution() {
}

Execution* Execution::Instance() {
    static Execution ins;
    return &ins;
}

void Execution::Init(std::shared_ptr<db::Db>& db) {
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    (void)ThreadLocalEvm();

// 	evmc_loader_error_code ec = EVMC_LOADER_UNSPECIFIED_ERROR;
//     evm_ = evmc::VM{ evmc_load_and_configure("./third_party/evmone/build/lib64/libevmone.so", &ec)};
// 	if (ec != EVMC_LOADER_SUCCESS) {
// 		const auto error = evmc_last_error_msg();
// 		if (error != nullptr)
// 			std::cerr << error << "\n";
// 		else
// 			std::cerr << "Loading error " << ec << "\n";
//         SHARDORA_FATAL("evm.set_option error.");
//         return;
// 	}
//
//     if (evm_.set_option("trace", "0") != EVMC_SET_OPTION_SUCCESS) {
//         SHARDORA_FATAL("evm.set_option error.");
//         return;
//     }

    // storage_map_ = new common::LimitHashMap<std::string, std::string, 1024>[common::kMaxThreadCount];
}

// No longer called — account_exists now uses ShardorahainHost::view_block_chain_ directly.
bool Execution::IsAddressExists(const std::string& addr) {
    SHARDORA_DEBUG("IsAddressExists called but deprecated, addr: %s",
        common::Encode::HexEncode(addr).c_str());
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

bool Execution::GetStorage(
        const evmc::address& addr,
        const evmc::bytes32& key,
        evmc::bytes32* res_val) {
    auto str_key = std::string((char*)addr.bytes, sizeof(addr.bytes)) +
        std::string((char*)key.bytes, sizeof(key.bytes));
    std::string val;
    auto res = prefix_db_->GetTemporaryKv(str_key, &val);
    if (!res) {
        return false;
    }

    block::protobuf::KeyValueInfo kv_info;
    if (!kv_info.ParseFromString(val)) {
        return false;
    }

    uint32_t offset = 0;
    uint32_t length = sizeof(res_val->bytes);
    if (kv_info.value().size() < sizeof(res_val->bytes)) {
        offset = sizeof(res_val->bytes) - kv_info.value().size();
        length = kv_info.value().size();
    }

    memcpy(res_val->bytes + offset, kv_info.value().c_str(), length);
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
    return prefix_db_->GetTemporaryKv(str_key, val);
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
        ShardorahainHost& host,
        evmc::Result* out_res) {
    auto btime = common::TimeUtils::TimestampMs();
    const size_t code_size = bytes_code.size();
    if (code_size <= kContractHead.size() ||
            from_address.size() != common::kUnicastAddressLength ||
            to_address.size() != common::kUnicastAddressLength ||
            depth >= kContractCallMaxDepth) {
        SHARDORA_DEBUG("invalid params code_size: %u, from size: %u, "
            "to size: %u, depth: %u, gas_limit: %lu, from_address: %s, to_address: %s, origin_address: %s",
            code_size, from_address.size(), to_address.size(), depth, gas_limit,
            common::Encode::HexEncode(from_address).c_str(),
            common::Encode::HexEncode(to_address).c_str(),
            common::Encode::HexEncode(origin_address).c_str());
        // //assert(false);
        return kShardoravmError;
    }

    int64_t gas = gas_limit;
    auto rev = EVMC_LATEST_STABLE_REVISION;
    auto create_gas = gas_limit;
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = static_cast<int32_t>(depth);
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
    SHARDORA_DEBUG("now call contract, msg sender: %s, mode: %d, from: %s, "
        "to: %s, value: %lu, gas limit: %lu, bytes_code.size: %ld, input: %s",
        common::Encode::HexEncode(std::string((char*)msg.sender.bytes, 20)).c_str(),
        call_mode,
        common::Encode::HexEncode(from_address).c_str(),
        common::Encode::HexEncode(to_address).c_str(),
        value,
        msg.gas,
        bytes_code.size(),
        common::Encode::HexEncode(str_input).c_str());
    if (call_mode == kJustCreate || call_mode == kCreateAndCall || call_mode == kCreate2) {
        msg.kind = EVMC_CREATE;
        if (call_mode == kCreate2) {
            msg.kind = EVMC_CREATE2;
        }

        *out_res = ThreadLocalEvm().execute(
            host,
            rev,
            msg,
            (uint8_t*)bytes_code.c_str(),
            bytes_code.size());
        if (out_res->status_code != EVMC_SUCCESS) {
            const auto gas_used = msg.gas - out_res->gas_left;
            SHARDORA_ERROR("out_res->status_code != EVMC_SUCCESS.nResult: %d, EVMC_SUCCESS: %d, "
                "gas_used: %lu, gas limit: %lu, codes: %s, from: %s, to: %s, output: %s",
                (int32_t)out_res->status_code, (int32_t)EVMC_SUCCESS, gas_used, create_gas,
                "common::Encode::HexEncode(bytes_code).c_str()",
                common::Encode::HexEncode(from_address).c_str(),
                common::Encode::HexEncode(to_address).c_str(),
                common::Encode::HexEncode(SafeEvmcOutput(*out_res)).c_str());
            return kShardoravmSuccess;
        } else {
            const auto gas_used = msg.gas - out_res->gas_left;
            SHARDORA_DEBUG("out_res->status_code == EVMC_SUCCESS.nResult: %d, gas_used: %lu, gas limit: %lu, codes: %s",
                (int32_t)out_res->status_code, gas_used, create_gas, "common::Encode::HexEncode(bytes_code).c_str()");
        }

        host.create_bytes_code_ = SafeEvmcOutput(*out_res);
        if (call_mode == kJustCreate || call_mode == kCreate2) {
            return kShardoravmSuccess;
        }

        msg.gas = out_res->gas_left;
        auto& created_account = host.accounts_[msg.recipient];
        created_account.code.assign(
            reinterpret_cast<const uint8_t*>(host.create_bytes_code_.data()),
            reinterpret_cast<const uint8_t*>(host.create_bytes_code_.data()) + host.create_bytes_code_.size());
        exec_code_data = created_account.code.data();
        exec_code_size = created_account.code.size();
    } else {
        exec_code_data = (uint8_t*)bytes_code.c_str();
        exec_code_size = bytes_code.size();
    }

    auto src_gas_left = out_res->gas_left;
    *out_res = ThreadLocalEvm().execute(host, rev, msg, exec_code_data, exec_code_size);
    auto etime = common::TimeUtils::TimestampMs();
    SHARDORA_DEBUG("execute res: %d, from: %s, to: %s, gas_limit: %lu, "
        "src_gas_left: %lu, gas_left: %lu, gas_refund: %lu, use time: %lu, output: %s",
        (int32_t)out_res->status_code,
        common::Encode::HexEncode(from_address).c_str(),
        common::Encode::HexEncode(to_address).c_str(),
        gas,
        src_gas_left,
        out_res->gas_left,
        out_res->gas_refund,
        (etime - btime),
        common::Encode::HexEncode(SafeEvmcOutput(*out_res)).c_str());
    return kShardoravmSuccess;
}

}  // namespace shardoravm

}  //namespace shardora
