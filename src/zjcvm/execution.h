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

    bool AddressWarm(uint8_t thread_idx, const evmc::address& addr) {
        auto str_addr = std::string((char*)addr.bytes, sizeof(addr.bytes));
        assert(thread_idx < common::kMaxThreadCount);
        if (address_exists_set_[thread_idx].exists(str_addr)) {
            return true;
        }

        return false;
    }

    bool StorageKeyWarm(
            uint8_t thread_idx,
            const evmc::address& addr,
            const evmc::bytes32& key) {
        auto str_key = std::string((char*)addr.bytes, sizeof(addr.bytes)) +
            std::string((char*)key.bytes, sizeof(key.bytes));
        return storage_map_[thread_idx].exists(str_key);
    }

    void NewBlockWithTx(
            uint8_t thread_idx,
            const std::shared_ptr<block::protobuf::Block>& block_item,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        if (tx.step() != pools::protobuf::kContractUserCreateCall &&
                tx.step() != pools::protobuf::kContractExcute) {
            return;
        }

        for (int32_t i = 0; i < tx.storages_size(); ++i) {
            if (tx.storages(i).key() == protos::kCreateContractBytesCode) {
                continue;
            }


            if (tx.storages(i).val_size() > 32) {
                std::string val;
                if (!prefix_db_->GetTemporaryKv(tx.storages(i).val_hash(), &val)) {
                    continue;
                }

                UpdateStorage(thread_idx, tx.storages(i).key(), val, db_batch);
            } else {
                UpdateStorage(thread_idx, tx.storages(i).key(), tx.storages(i).val_hash(), db_batch);
            }
        }
    }

    void UpdateStorage(
            uint8_t thread_idx,
            const std::string& key,
            const std::string& val,
            db::DbWriteBatch& db_batch) {
        storage_map_[thread_idx].add(str_key, val);
        prefix_db_->SaveTemporaryKv(key, val, db_batch);
    }

    evmc::bytes32 GetStorage(
            uint8_t thread_idx,
            const evmc::address& addr,
            const evmc::bytes32& key) {
        auto str_key = std::string((char*)addr.bytes, sizeof(addr.bytes)) +
            std::string((char*)key.bytes, sizeof(key.bytes));
        std::string val;
        if (!storage_map_[thread_idx].get(str_key, &val)) {
            // get from db and add to memory cache
            if (prefix_db_->GetTemporaryKv(str_key, &val)) {
                storage_map_[thread_idx].add(str_key, val);
            }
        }

        if (val.empty()) {
            return evmc::bytes32{};
        }

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
        auto res = prefix_db_->GetTemporaryKv(str_key, val);
        if (res) {
            storage_map_[thread_idx].add(str_key, *val);
        }
        
        return res;
    }

private:
    Execution();
    ~Execution();

    evmc::VM evm_;
    common::StringUniqueSet<10240, 16>* address_exists_set_ = nullptr;
    common::UniqueMap<std::string, std::string, 10240, 16>* storage_map_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(Execution);
};

}  // namespace zjcvm

}  //namespace zjchain

