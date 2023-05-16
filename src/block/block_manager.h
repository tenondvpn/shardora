#pragma once

#include <deque>

#include "block/block_utils.h"
#include "ck/ck_client.h"
#include "common/config.h"
#include "common/node_members.h"
#include "common/thread_safe_queue.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/to_txs_pools.h"
#include "protos/block.pb.h"
#include "protos/zbft.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace pools{
    class TxPoolManager;
    class ShardStatistic;
}

namespace block {

class AccountManager;
class BlockManager {
public:
    BlockManager();
    ~BlockManager();
    int Init(
        std::shared_ptr<AccountManager>& account_mgr,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<pools::ShardStatistic>& statistic_mgr,
        std::shared_ptr<security::Security>& security,
        const std::string& local_id,
        DbBlockCallback new_block_callback);
    void NetworkNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item);
    void OnTimeBlock(
        uint8_t thread_idx,
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    void ConsensusAddBlock(
        uint8_t thread_idx,
        const BlockToDbItemPtr& block_item);
    int GetBlockWithHeight(
        uint32_t network_id,
        uint32_t pool_index,
        uint64_t height,
        block::protobuf::Block& block_item);
    void NewBlockWithTx(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void SetMaxConsensusShardingId(uint32_t sharding_id) {
        max_consensus_sharding_id_ = sharding_id;
    }

    void SetCreateToTxFunction(pools::CreateConsensusItemFunction func) {
        create_to_tx_cb_ = func;
    }

    void SetCreateStatisticTxFunction(pools::CreateConsensusItemFunction func) {
        create_statistic_tx_cb_ = func;
    }

    void SetCreateElectTxFunction(pools::CreateConsensusItemFunction func) {
        create_elect_tx_cb_ = func;
    }

    void SetCreateCrossTxFunction(pools::CreateConsensusItemFunction func) {
        cross_tx_cb_ = func;
    }

    void CreateToTx(uint8_t thread_idx);
    void CreateStatisticTx(uint8_t thread_idx);
    void OnNewElectBlock(uint32_t sharding_id, common::MembersPtr& members);
    pools::TxItemPtr GetToTx(uint32_t pool_index, bool leader);
    pools::TxItemPtr GetStatisticTx(uint32_t pool_index, bool leader);
    pools::TxItemPtr GetElectTx(uint32_t pool_index, const std::string& tx_hash);
    pools::TxItemPtr GetCrossTx(uint32_t pool_index, bool leader);
    void LoadLatestBlocks(uint8_t thread_idx);
    // just genesis call
    void GenesisAddAllAccount(
        uint32_t des_sharding_id,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch);

private:
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void HandleCrossShardingToTxs(const transport::MessagePtr& msg_ptr);
    void HandleCrossShardingStatisticTxs(const transport::MessagePtr& msg_ptr);
    void HandleElectBlock(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage(const transport::MessagePtr& msg_ptr);
    void HandleToTxsMessage(const transport::MessagePtr& msg_ptr, bool recreate);
    void HandleStatisticMessage(const transport::MessagePtr& msg_ptr);
    void HandleAllConsensusBlocks(uint8_t thread_idx);
    void AddNewBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch);
    void HandleNormalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleStatisticTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleElectTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleLocalNormalToTx(
        uint8_t thread_idx,
        const pools::protobuf::ToTxMessage& to_txs,
        uint32_t step,
        const std::string& heights_hash);
    void HandleJoinElectTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void AddMiningToken(
        const std::string& block_hash,
        uint8_t thread_idx,
        const elect::protobuf::ElectBlock& elect_block);
    void RootHandleNormalToTx(
        uint8_t thread_idx,
        uint64_t height,
        pools::protobuf::ToTxMessage& to_txs);
    void CreateNewAddress();
    void HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        const pools::protobuf::ElectStatistic& elect_statistic,
        db::DbWriteBatch& db_batch);

    static const uint64_t kCreateToTxPeriodMs = 10000u;

    std::shared_ptr<AccountManager> account_mgr_ = nullptr;
    common::ThreadSafeQueue<BlockToDbItemPtr>* consensus_block_queues_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<pools::ToTxsPools> to_txs_pool_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    uint64_t prev_create_to_tx_ms_ = 0;
    uint64_t prev_create_statistic_tx_ms_ = 0;
    common::BftMemberPtr to_tx_leader_ = nullptr;
    uint32_t max_consensus_sharding_id_ = 3;
    std::string local_id_;
    std::shared_ptr<BlockTxsItem> to_txs_[network::kConsensusShardEndNetworkId] = { nullptr };
    std::shared_ptr<BlockTxsItem> shard_statistic_tx_ = nullptr;
    std::shared_ptr<BlockTxsItem> cross_statistic_tx_ = nullptr;
    std::unordered_map<uint32_t, std::shared_ptr<BlockTxsItem>> shard_elect_tx_;
    pools::CreateConsensusItemFunction create_to_tx_cb_ = nullptr;
    pools::CreateConsensusItemFunction create_statistic_tx_cb_ = nullptr;
    pools::CreateConsensusItemFunction create_elect_tx_cb_ = nullptr;
    pools::CreateConsensusItemFunction cross_tx_cb_ = nullptr;
    uint32_t prev_pool_index_ = network::kRootCongressNetworkId;
    std::shared_ptr<ck::ClickHouseClient> ck_client_ = nullptr;
    transport::MessagePtr to_txs_msg_ = nullptr;
    uint64_t prev_to_txs_tm_us_ = 0;
    DbBlockCallback new_block_callback_ = nullptr;
    std::shared_ptr<pools::ShardStatistic> statistic_mgr_ = nullptr;
    uint64_t latest_timeblock_height_ = 0;
    uint64_t consensused_timeblock_height_ = 0;
    std::unordered_map<uint32_t, std::map<
        uint64_t,
        std::shared_ptr<pools::protobuf::ElectStatistic>>> shard_timeblock_statistic_;

    DISALLOW_COPY_AND_ASSIGN(BlockManager);
};

}  // namespace block

}  // namespace zjchain

