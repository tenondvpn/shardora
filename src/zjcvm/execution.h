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
#include "zjcvm/storage_lru_map.h"

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
    void NewBlock(
            const view_block::protobuf::ViewBlockItem& view_block,
            db::DbWriteBatch& db_batch) {
        auto& block = view_block.block_info();
        if (block.height() <= pools_max_heights_[view_block.qc().pool_index()]) {
            // ZJC_DEBUG("block.height() <= pools_max_heights_[view_block.qc().pool_index()] "
            //     " %lu, %lu", 
            //     block.height(), 
            //     pools_max_heights_[view_block.qc().pool_index()]);
            // ZJC_INFO("failed save contract prepayment pool: %u, height: %lu",
            //     view_block.qc().pool_index(),
            //     block.height());
            // assert(false);
            return;
        }

        const auto& tx_list = block.tx_list();
        for (int32_t i = 0; i < tx_list.size(); ++i) {
            NewBlockWithTx(view_block, tx_list[i], db_batch);
        }

        pools_max_heights_[view_block.qc().pool_index()] = block.height();
        ZJC_DEBUG("success new block pool: %u, height: %lu",
            view_block.qc().pool_index(),
            block.height());
    }
    
    void NewBlockWithTx(
            const view_block::protobuf::ViewBlockItem& view_block,
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
    StorageLruMap<1024> storage_map_[common::kMaxThreadCount];
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<block::AccountManager> acc_mgr_ = nullptr;
    uint64_t pools_max_heights_[common::kInvalidPoolIndex] = { 0 };

    DISALLOW_COPY_AND_ASSIGN(Execution);
};

}  // namespace zjcvm

}  //namespace shardora

