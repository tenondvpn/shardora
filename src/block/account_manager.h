#pragma once

#include <memory>
#include <queue>
#include <unordered_map>

#include "common/config.h"
#include "common/tick.h"
#include "common/limit_heap.h"
#include "common/limit_hash_set.h"
#include "block/block_utils.h"
#include "db/db.h"
#include "pools/tx_pool_manager.h"
#include "protos/block.pb.h"
#include "protos/zbft.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"

namespace zjchain {

namespace block {

class AccountManager {
public:
    AccountManager();
    ~AccountManager();
    int Init(
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr);
    void NewBlockWithTx(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    protos::AddressInfoPtr GetAccountInfo(uint8_t thread_idx, const std::string& acc_id);
    protos::AddressInfoPtr GetAcountInfoFromDb(const std::string& acc_id);
    bool AccountExists(uint8_t thread_idx, const std::string& acc_id);
    int GetAddressConsensusNetworkId(
        uint8_t thread_idx,
        const std::string& addr,
        uint32_t* network_id);
    protos::AddressInfoPtr GetContractInfoByAddress(uint8_t thread_idx, const std::string& address);
    void PrintPoolHeightTree(uint32_t pool_idx);
    void SetMaxHeight(uint32_t pool_idx, uint64_t height);
    int HandleRefreshHeightsReq(const transport::MessagePtr& msg_ptr);
    int HandleRefreshHeightsRes(const transport::MessagePtr& msg_ptr);
    std::shared_ptr<address::protobuf::AddressInfo>& pools_address_info(uint32_t pool_idx) {
        if (pool_idx == common::kRootChainPoolIndex) {
            return root_pool_address_info_;
        }

        return pool_address_info_[pool_idx % common::kImmutablePoolSize];
    }

    const std::string& GetTxValidAddress(const block::protobuf::BlockTx& tx_info);

private:
    void SetPool(
        uint32_t pool_index,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch);
    void SendRefreshHeightsRequest();
    void SendRefreshHeightsResponse(const transport::protobuf::Header& header);
    void HandleNormalFromTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleContractPrepayment(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleCreateContract(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleCreateContractByRootFrom(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleContractCreateByRootTo(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleLocalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleRootCreateAddressTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleContractExecuteTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void CreatePoolsAddressInfo();
    void HandleJoinElectTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void UpdateAccountsThread();
    void InitLoadAllAddress();

    inline bool isContractCreateTx(const block::protobuf::BlockTx& tx) {
        return tx.has_contract_code();
    }

    static const uint64_t kCheckMissingHeightPeriod = 3000000llu;
    static const uint64_t kFushTreeToDbPeriod = 6000000llu;
    static const uint64_t kRefreshPoolMaxHeightPeriod = 4000000llu;

    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    common::Tick check_missing_height_tick_;
    common::Tick flush_db_tick_;
    common::Tick refresh_pool_max_height_tick_;
    common::Tick merge_updated_accounts_tick_;
    uint64_t prev_refresh_heights_tm_{ 0 };
    common::LimitHashSet<std::string> block_hash_limit_set_{ 2048u };
    bool inited_{ false };
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<address::protobuf::AddressInfo> pool_address_info_[common::kImmutablePoolSize] = { nullptr };
    std::shared_ptr<address::protobuf::AddressInfo> root_pool_address_info_ = nullptr ;
    std::unordered_map<std::string, protos::AddressInfoPtr> thread_address_map_[common::kMaxThreadCount];
    common::ThreadSafeQueue<protos::AddressInfoPtr> thread_update_accounts_queue_[common::kMaxThreadCount];
    std::shared_ptr<std::thread> merge_update_accounts_thread_ = nullptr;
    common::ThreadSafeQueue<protos::AddressInfoPtr> thread_valid_accounts_queue_[common::kMaxThreadCount];

    DISALLOW_COPY_AND_ASSIGN(AccountManager);
};

}  // namespace block

}  // namespace zjchain
