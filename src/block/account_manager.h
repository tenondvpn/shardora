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
    void NewBlockWithTx(
        const view_block::protobuf::ViewBlockItem& view_block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    protos::AddressInfoPtr GetAccountInfo(const std::string& acc_id);
    protos::AddressInfoPtr GetAcountInfoFromDb(const std::string& acc_id);
    bool AccountExists(const std::string& acc_id);
    int GetAddressConsensusNetworkId(
        const std::string& addr,
        uint32_t* network_id);
    protos::AddressInfoPtr GetContractInfoByAddress(const std::string& address);
    void PrintPoolHeightTree(uint32_t pool_idx);
    void SetMaxHeight(uint32_t pool_idx, uint64_t height);
    int HandleRefreshHeightsReq(const transport::MessagePtr& msg_ptr);
    int HandleRefreshHeightsRes(const transport::MessagePtr& msg_ptr);
    std::shared_ptr<address::protobuf::AddressInfo> pools_address_info(uint32_t pool_idx) {
        if (pool_idx == common::kImmutablePoolSize) {
            return GetAccountInfo(immutable_pool_addr_);
        }

        return GetAccountInfo(pool_base_addrs_[pool_idx]);
    }

    const std::string& GetTxValidAddress(const block::protobuf::BlockTx& tx_info);

private:
    void SendRefreshHeightsRequest();
    void SendRefreshHeightsResponse(const transport::protobuf::Header& header);
    void HandleNormalFromTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleContractPrepayment(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleCreateContract(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleCreateContractByRootFrom(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    // void HandleContractCreateByRootTo(
    //     const view_block::protobuf::ViewBlockItem& view_block,
    //     const block::protobuf::BlockTx& tx,
    //     db::DbWriteBatch& db_batch);
    void HandleLocalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleRootCreateAddressTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleContractExecuteTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void CreatePoolsAddressInfo();
    void HandleJoinElectTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleCreateGenesisAcount(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void UpdateAccountsThread();
    void RunUpdateAccounts();
    void UpdateContractPrepayment(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);

    static const uint64_t kCheckMissingHeightPeriod = 3000000llu;
    static const uint64_t kFushTreeToDbPeriod = 6000000llu;
    static const uint64_t kRefreshPoolMaxHeightPeriod = 4000000llu;

    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    uint64_t prev_refresh_heights_tm_{ 0 };
    common::LimitHashSet<std::string> block_hash_limit_set_{ 2048u };
    bool inited_{ false };
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    common::ThreadSafeQueue<protos::AddressInfoPtr> thread_update_accounts_queue_[common::kMaxThreadCount];
    std::shared_ptr<std::thread> merge_update_accounts_thread_ = nullptr;
    // common::ThreadSafeQueue<protos::AddressInfoPtr> thread_valid_accounts_queue_[common::kMaxThreadCount];
    volatile bool destroy_ = false;
    std::condition_variable update_acc_con_;
    std::mutex update_acc_mutex_;
    std::shared_ptr<std::thread> update_acc_thread_ = nullptr;
    std::condition_variable thread_wait_conn_;
    std::mutex thread_wait_mutex_;
    volatile bool thread_valid_[common::kMaxThreadCount] = {false};
    AccountLruMap<102400> account_lru_map_;
    std::string immutable_pool_addr_;
    std::string pool_base_addrs_[common::kImmutablePoolSize];

    DISALLOW_COPY_AND_ASSIGN(AccountManager);
};

}  // namespace block

}  // namespace shardora
