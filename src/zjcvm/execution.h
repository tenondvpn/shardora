#pragma once

#include <memory>
#include <protos/pools.pb.h>

#include "common/limit_hash_map.h"
#include "common/unique_set.h"
#include "common/utils.h"
#include "evmc/evmc.hpp"
#include "evmc/mocked_host.hpp"
#include "protos/address.pb.h"
#include "protos/prefix_db.h"

namespace shardora {

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
    bool IsAddressExists(const std::string& addr);
    bool AddressWarm(const evmc::address& addr);
    bool StorageKeyWarm(
            const evmc::address& addr,
            const evmc::bytes32& key);
    void NewBlockWithTx(
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch);
    void UpdateStorage(
            const std::string& key,
            const std::string& val,
            db::DbWriteBatch& db_batch);
    bool GetStorage(
            const evmc::address& addr,
            const evmc::bytes32& key,
            evmc::bytes32* res_val);
    bool GetStorage(
            const evmc::address& addr,
            const std::string& key,
            std::string* val);
    bool GetStorage(
            const std::string& addr,
            const std::string& key,
            std::string* val);

private:
    Execution();
    ~Execution();

    evmc::VM evm_;
    common::LimitHashMap<std::string, std::string, 1024>* storage_map_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(Execution);
};

}  // namespace zjcvm

}  //namespace shardora

