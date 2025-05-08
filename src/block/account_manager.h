#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>

#include "common/config.h"
#include "common/tick.h"
#include "common/limit_heap.h"
#include "common/limit_hash_set.h"
#include "block/account_lru_map.h"
#include "block/block_utils.h"
#include "db/db.h"
#include "pools/tx_pool_manager.h"
#include "protos/block.pb.h"
#include "protos/zbft.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"

namespace shardora {

namespace block {

class AccountManager {
public:
    AccountManager();
    ~AccountManager();
    int Init(
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr);
    void AddNewBlock(
        const view_block::protobuf::ViewBlockItem& view_block_item);
    protos::AddressInfoPtr GetAccountInfo(const std::string& acc_id);
    protos::AddressInfoPtr GetAcountInfoFromDb(const std::string& acc_id);
    bool AccountExists(const std::string& acc_id);
    int GetAddressConsensusNetworkId(
        const std::string& addr,
        uint32_t* network_id);
    protos::AddressInfoPtr GetContractInfoByAddress(const std::string& address);
    void PrintPoolHeightTree(uint32_t pool_idx);
    std::shared_ptr<address::protobuf::AddressInfo> pools_address_info(uint32_t pool_idx) {
        if (pool_idx == common::kImmutablePoolSize) {
            return GetAccountInfo(immutable_pool_addr_);
        }

        return GetAccountInfo(pool_base_addrs_[pool_idx]);
    }

    const std::string& GetTxValidAddress(const block::protobuf::BlockTx& tx_info);

    const std::string& pool_base_addrs(uint32_t pool_idx) const {
        if (pool_idx >= common::kImmutablePoolSize) {
            return immutable_pool_addr_;
        }

        return pool_base_addrs_[pool_idx];
    }

private:
    void CreatePoolsAddressInfo();
   
    static const uint64_t kCheckMissingHeightPeriod = 3000000llu;
    static const uint64_t kFushTreeToDbPeriod = 6000000llu;
    static const uint64_t kRefreshPoolMaxHeightPeriod = 4000000llu;

    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    AccountLruMap<102400> account_lru_map_;
    std::string immutable_pool_addr_;
    std::string pool_base_addrs_[common::kImmutablePoolSize];

    DISALLOW_COPY_AND_ASSIGN(AccountManager);
};

}  // namespace block

}  // namespace shardora
