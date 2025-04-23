#pragma once

#include <deque>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "block/block_utils.h"
#include "ck/ck_client.h"
#include "common/config.h"
#include "common/limit_hash_map.h"
#include "common/node_members.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "contract/contract_manager.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "pools/to_txs_pools.h"
#include "protos/block.pb.h"
#include "protos/zbft.pb.h"
#include "protos/prefix_db.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/multi_thread.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace pools{
    class TxPoolManager;
    class ShardStatistic;
}

namespace consensus {
    class HotstuffManager;
}

namespace block {

class AccountManager;
class BlockManager {
public:
    BlockManager(transport::MultiThreadHandler& net_handler_, std::shared_ptr<ck::ClickHouseClient> ck_client);
    ~BlockManager();
    int Init(
        std::shared_ptr<AccountManager>& account_mgr,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<pools::ShardStatistic>& statistic_mgr,
        std::shared_ptr<security::Security>& security,
        std::shared_ptr<contract::ContractManager>& contract_mgr,
        std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr,
        const std::string& local_id,
        DbBlockCallback new_block_callback);
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    void ConsensusAddBlock(const std::shared_ptr<hotstuff::ViewBlockInfo>& block_item);
    int GetBlockWithHeight(
        uint32_t network_id,
        uint32_t pool_index,
        uint64_t height,
        view_block::protobuf::ViewBlockItem& block_item);
    void OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height, common::MembersPtr& members);
    pools::TxItemPtr GetStatisticTx(uint32_t pool_index, const std::string&);
    pools::TxItemPtr GetElectTx(uint32_t pool_index, const std::string& tx_hash);
    pools::TxItemPtr GetToTx(uint32_t pool_index, const std::string& tx_hash);
    void LoadLatestBlocks();
    // just genesis call
    void GenesisAddAllAccount(
        uint32_t des_sharding_id,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBatch& db_batch);
    void GenesisAddOneAccount(
        uint32_t des_sharding_id,
        const block::protobuf::BlockTx& tx,
        const uint64_t& latest_height,
        db::DbWriteBatch& db_batch);
    bool ShouldStopConsensus();
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    bool HasSingleTx(
        const transport::MessagePtr& msg_ptr,
        uint32_t pool_index, 
        pools::CheckAddrNonceValidFunction tx_valid_func);

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

private:
    typedef std::map<uint64_t, std::shared_ptr<BlockTxsItem>, std::greater<uint64_t>> StatisticMap;
    bool HasToTx(uint32_t pool_index, pools::CheckAddrNonceValidFunction tx_valid_func);
    bool HasStatisticTx(uint32_t pool_index, pools::CheckAddrNonceValidFunction tx_valid_func);
    bool HasElectTx(uint32_t pool_index, pools::CheckAddrNonceValidFunction tx_valid_func);
    void HandleAllNewBlock();
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void ConsensusTimerMessage(const transport::MessagePtr& message);
    pools::TxItemPtr HandleToTxsMessage(
        const pools::protobuf::ShardToTxItem& msg_ptr);
    void HandleAllConsensusBlocks();
    void AddNewBlock(
        const std::shared_ptr<hotstuff::ViewBlockInfo>& block_item);
    void HandleNormalToTx(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr,
        const block::protobuf::BlockTx& tx);
    void HandleStatisticTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void HandleElectTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void ConsensusShardHandleRootCreateAddress(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx);
    void HandleLocalNormalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const pools::protobuf::ToTxMessage& to_txs,
        const block::protobuf::BlockTx& tx);
    void createConsensusLocalToTxs(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        std::unordered_map<std::string, std::shared_ptr<localToTxInfo>>& addr_amount_map);
    void createContractCreateByRootToTxs(
        std::vector<std::shared_ptr<localToTxInfo>>& contract_create_tx_infos);
    void AddMiningToken(
        const view_block::protobuf::ViewBlockItem& view_block,
        const elect::protobuf::ElectBlock& elect_block);
    void RootHandleNormalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        pools::protobuf::ToTxMessage& to_txs);
    void HandleStatisticBlock(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        const pools::protobuf::ElectStatistic& elect_statistic);
    void CreateStatisticTx();
    void PopTxTicker();

    inline bool IsTimeblockHeightStatisticDone(uint64_t timeblock_height) {
        return latest_statistic_timeblock_height_ >= timeblock_height;
    }

    inline void MarkDoneTimeblockHeightStatistic(uint64_t timeblock_height) {
        latest_statistic_timeblock_height_ = timeblock_height;
    }

    static const uint64_t kCreateToTxPeriodMs = 10000lu;
    static const uint64_t kRetryStatisticPeriod = 3000lu;
    static const uint64_t kStatisticTimeoutMs = 20000lu;
    static const uint64_t kToTimeoutMs = 10000lu;
    static const uint64_t kStatisticValidTimeout = 15000lu;
    static const uint64_t kToValidTimeout = 1500lu;
    static const uint64_t kElectTimeout = 20000lu;
    static const uint64_t kElectValidTimeout = 3000000lu;
    static const uint32_t kEachTimeHandleBlocksCount = 64u;

    std::shared_ptr<AccountManager> account_mgr_ = nullptr;
    common::ThreadSafeQueue<std::shared_ptr<hotstuff::ViewBlockInfo>>* consensus_block_queues_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<pools::ToTxsPools> to_txs_pool_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    uint64_t prev_create_to_tx_ms_ = 0;
    uint64_t prev_retry_create_statistic_tx_ms_ = 0;
    uint32_t max_consensus_sharding_id_ = 3;
    std::shared_ptr<BlockTxsItem> shard_elect_tx_[network::kConsensusShardEndNetworkId];
    pools::CreateConsensusItemFunction create_to_tx_cb_ = nullptr;
    pools::CreateConsensusItemFunction create_statistic_tx_cb_ = nullptr;
    pools::CreateConsensusItemFunction create_elect_tx_cb_ = nullptr;
    uint32_t prev_pool_index_ = network::kRootCongressNetworkId;
    transport::MultiThreadHandler& net_handler_;
    std::shared_ptr<ck::ClickHouseClient> ck_client_ = nullptr;
    DbBlockCallback new_block_callback_ = nullptr;
    std::shared_ptr<pools::ShardStatistic> statistic_mgr_ = nullptr;
    uint64_t latest_timeblock_height_ = 0;
    uint64_t prev_timeblock_tm_sec_ = 0;
    uint64_t latest_timeblock_tm_sec_ = 0;
    uint64_t prev_timeblock_height_ = 0;
    std::shared_ptr<pools::protobuf::ToTxHeights> statistic_heights_ptr_ = nullptr;
//     std::shared_ptr<pools::protobuf::ToTxHeights> to_tx_heights_ptr_ = nullptr;
    common::MembersPtr latest_members_ = nullptr;
    uint64_t latest_elect_height_ = 0;
    int32_t leader_create_to_heights_index_ = 0;
    int32_t leader_create_statistic_heights_index_ = 0;
    StatisticMap shard_statistics_map_;
    common::ThreadSafeQueue<std::shared_ptr<StatisticMap>> shard_statistics_map_ptr_queue_;
    std::shared_ptr<StatisticMap> got_latest_statistic_map_ptr_[2] = { nullptr };
    uint32_t valid_got_latest_statistic_map_ptr_index_ = 0;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_ = nullptr;
    uint64_t prev_create_statistic_tx_tm_us_ = 0;
    uint64_t prev_timer_ms_ = 0;
    common::Tick pop_tx_tick_;
    std::shared_ptr<view_block::protobuf::ViewBlockItem> latest_to_block_ptr_[2] = { nullptr };
    uint32_t latest_to_block_ptr_index_ = 0;
    std::map<std::string, pools::TxItemPtr> heights_str_map_;
    uint32_t leader_prev_get_to_tx_tm_ = 0;
    std::shared_ptr<std::thread> handle_consensus_block_thread_;
    std::mutex wait_mutex_;
    std::condition_variable wait_con_;
    uint64_t latest_statistic_timeblock_height_ = 0; // memorize the latest timeblock height that has gathered statistic

    DISALLOW_COPY_AND_ASSIGN(BlockManager);
};

}  // namespace block

}  // namespace shardora

