#pragma once

#include <memory>
#include <protos/pools.pb.h>

#include "common/unique_map.h"
#include "common/unique_set.h"
#include "common/utils.h"
#include "evmc/evmc.hpp"
#include "evmc/mocked_host.hpp"
#include "protos/address.pb.h"
#include "protos/prefix_db.h"

namespace zjchain {

namespace block {
    class AccountManager;
};

namespace zjcvm {

class ZjchainHost;
class Execution {
public:
    static Execution* Instance();
    void Init(std::shared_ptr<db::Db>& db, std::shared_ptr<block::AccountManager>& acc_mgr);
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
    bool IsAddressExists(uint8_t thread_idx, const std::string& addr);
    bool AddressWarm(uint8_t thread_idx, const evmc::address& addr);
    bool StorageKeyWarm(
            uint8_t thread_idx,
            const evmc::address& addr,
            const evmc::bytes32& key);
    void NewBlockWithTx(
            uint8_t thread_idx,
            const std::shared_ptr<block::protobuf::Block>& block_item,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch);
    void UpdateStorage(
            uint8_t thread_idx,
            const std::string& key,
            const std::string& val,
            db::DbWriteBatch& db_batch);
    evmc::bytes32 GetStorage(
            uint8_t thread_idx,
            const evmc::address& addr,
            const evmc::bytes32& key);
    bool GetStorage(
            uint8_t thread_idx,
            const evmc::address& addr,
            const std::string& key,
            std::string* val);

private:
    Execution();
    ~Execution();

    evmc::VM evm_;
    common::UniqueMap<std::string, std::string, 256, 16>* storage_map_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(Execution);
};

}  // namespace zjcvm

}  //namespace zjchain

