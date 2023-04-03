#pragma once

#include <unordered_map>
#include <queue>
#include <memory>

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
        uint8_t thread_count,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr);
    void NewBlockWithTx(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    protos::AddressInfoPtr GetAcountInfo(uint8_t thread_idx, const std::string& acc_id);
    protos::AddressInfoPtr GetAcountInfo(
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx_info);
    protos::AddressInfoPtr GetAcountInfoFromDb(const std::string& acc_id);
    bool AccountExists(uint8_t thread_idx, const std::string& acc_id);
    int GetAddressConsensusNetworkId(
        uint8_t thread_idx,
        const std::string& addr,
        uint32_t* network_id);
    protos::AddressInfoPtr GetContractInfoByAddress(uint8_t thread_idx, const std::string& address);
    std::string GetPoolBaseAddr(uint32_t pool_index);
    void PrintPoolHeightTree(uint32_t pool_idx);
//     void FlushPoolHeightTreeToDb();
    void SetMaxHeight(uint32_t pool_idx, uint64_t height);
    int HandleRefreshHeightsReq(const transport::MessagePtr& msg_ptr);
    int HandleRefreshHeightsRes(const transport::MessagePtr& msg_ptr);
    std::string GetTxValidAddress(const block::protobuf::BlockTx& tx_info);
    std::shared_ptr<address::protobuf::AddressInfo>& single_to_address_info(uint32_t pool_idx) {
        return single_to_address_info_[pool_idx % common::kImmutablePoolSize];
    }

    std::shared_ptr<address::protobuf::AddressInfo>& single_local_to_address_info(uint32_t pool_idx) {
        return single_local_to_address_info_[pool_idx % common::kImmutablePoolSize];
    }

private:
    void SetPool(
        uint32_t pool_index,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch);
//     int HandleRootSingleBlockTx(
//         uint64_t height,
//         const block::protobuf::BlockTx& tx_info,
//         db::DbWriteBatch& db_batch);
//     int HandleElectBlock(
//         uint64_t height,
//         const block::protobuf::BlockTx& tx_info,
//         db::DbWriteBatch& db_batch);
//     int HandleTimeBlock(
//         uint64_t height,
//         const block::protobuf::BlockTx& tx_info);
//     int HandleFinalStatisticBlock(uint64_t height, const block::protobuf::BlockTx& tx_info);
//     void CheckMissingHeight();
    void RefreshPoolMaxHeight();
    void SendRefreshHeightsRequest();
    void SendRefreshHeightsResponse(const transport::protobuf::Header& header);
    void HandleNormalFromTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleLocalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void CreateNormalToAddressInfo();
    void CreateNormalLocalToAddressInfo();

    static const uint64_t kCheckMissingHeightPeriod = 3000000llu;
    static const uint64_t kFushTreeToDbPeriod = 6000000llu;
    static const uint64_t kRefreshPoolMaxHeightPeriod = 4000000llu;

    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    common::UniqueMap<std::string, protos::AddressInfoPtr>* address_map_ = nullptr;
    common::Tick check_missing_height_tick_;
    common::Tick flush_db_tick_;
    common::Tick refresh_pool_max_height_tick_;
    uint64_t prev_refresh_heights_tm_{ 0 };
    common::LimitHashSet<std::string> block_hash_limit_set_{ 2048u };
    bool inited_{ false };
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<address::protobuf::AddressInfo> single_to_address_info_[common::kImmutablePoolSize] = { nullptr };
    std::shared_ptr<address::protobuf::AddressInfo> single_local_to_address_info_[common::kImmutablePoolSize] = { nullptr };

    DISALLOW_COPY_AND_ASSIGN(AccountManager);
};

}  // namespace block

}  // namespace zjchain
