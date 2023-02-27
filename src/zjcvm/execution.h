#pragma once

#include <memory>

#include "common/unique_map.h"
#include "common/unique_set.h"
#include "common/utils.h"
#include "evmc/evmc.hpp"
#include "evmc/mocked_host.hpp"
#include "protos/address.pb.h"
#include "protos/prefix_db.h"

namespace zjchain {

namespace zjcvm {

class ZjchainHost;
class Execution {
public:
    static Execution* Instance();
    void Init(std::shared_ptr<db::Db>& db);
    int execute(
        const std::string& contract_address,
        const std::string& input,
        const std::string& from_address,
        const std::string& to_address,
        const std::string& origin_address,
        uint64_t value,
        uint64_t max_gas,
        uint32_t depth,
        uint32_t call_mode,
        ZjchainHost& host,
        evmc::Result* res);

    bool IsAddressExists(uint8_t thread_idx, const std::string& addr) {
        assert(thread_idx < common::kMaxThreadCount);
        if (address_exists_set_[thread_idx].exists(addr)) {
            return true;
        }

        // get from db and add to memory cache
        auto address_info = prefix_db_->GetAddressInfo(addr);
        if (address_info != nullptr) {
            address_exists_set_[thread_idx].add(addr);
            return true;
        }

        return false;
    }

    evmc::bytes32 GetStorage(
            uint8_t thread_idx,
            const evmc::address& addr,
            const evmc::bytes32& key) {
        auto str_key = std::string((char*)addr.bytes, sizeof(addr.bytes)) +
         std::string((char*)key.bytes, sizeof(key.bytes));
        std::string val;
        if (storage_map_[thread_idx].get(str_key, &val)) {
            evmc::bytes32 tmp_val{};
            uint32_t offset = 0;
            uint32_t length = sizeof(tmp_val.bytes);
            if (val.size() < sizeof(tmp_val.bytes)) {
                offset = sizeof(tmp_val.bytes) - val.size();
                length = val.size();
            }

            memcpy(tmp_val.bytes + offset, val.c_str(), length);
            return tmp_val;
        }

        // get from db and add to memory cache
        auto byte32 = prefix_db_->GetAddressStorage(addr, key);
        if (byte32) {
            storage_map_[thread_idx].add(
                str_key,
                std::string((char*)byte32.bytes, sizeof(byte32.bytes)));
        }
        
        return byte32;
    }

    bool GetStorage(
            uint8_t thread_idx,
            const evmc::address& addr,
            const std::string& key,
            std::string* val) {
        auto str_key = std::string((char*)addr.bytes, sizeof(addr.bytes)) + key;
        if (storage_map_[thread_idx].get(str_key, val)) {
            return true;
        }

        // get from db and add to memory cache
        auto res = prefix_db_->GetAddressStorage(addr, key, val);
        if (res) {
            storage_map_[thread_idx].add(str_key, *val);
        }
        
        return res;
    }

private:
    Execution();
    ~Execution();

    evmc::VM evm_;
    common::UniqueSet<std::string> address_exists_set_[common::kMaxThreadCount];
    common::UniqueMap<std::string, std::string> storage_map_[common::kMaxThreadCount];
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(Execution);
};

}  // namespace zjcvm

}  //namespace zjchain

