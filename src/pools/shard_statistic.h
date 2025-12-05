#pragma once

#include <unordered_map>
#include <atomic>
#include <queue>
#include <memory>
#include <random>

#include "common/bitmap.h"
#include "common/lof.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "common/utils.h"
#include "pools/tx_utils.h"
#include "protos/block.pb.h"
#include "protos/prefix_db.h"
#include "security/security.h"

namespace shardora {

namespace elect {
    class ElectManager;
};

namespace pools {

class TxPoolManager;
class ShardStatistic {
public:
    ShardStatistic(
            std::shared_ptr<elect::ElectManager>& elect_mgr,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<security::Security>& sec_ptr,
            std::shared_ptr<pools::TxPoolManager>& pools_mgr,
            std::shared_ptr<contract::ContractManager>& tmp_contract_mgr)
            : elect_mgr_(elect_mgr), secptr_(sec_ptr), pools_mgr_(pools_mgr), contract_mgr_(tmp_contract_mgr) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
        handle_block_thread_ = std::make_shared<std::thread>(
            std::bind(&ShardStatistic::ThreadCallback, this));
    }

    ~ShardStatistic() {
        destroy_ = true;
        handle_block_thread_->join();
    }
    int Init();
    void CallNewElectBlock(
        uint32_t sharding_id,
        uint64_t prepare_elect_height);
    void OnNewBlock(const std::shared_ptr<view_block::protobuf::ViewBlockItem>& block);
    // just block manager to call
    void CallTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    int StatisticWithHeights(
        pools::protobuf::ElectStatistic &elect_statistic,
        uint64_t statisticed_timeblock_height);


  private:
    void addHeightInfo2Statics(
        shardora::pools::protobuf::ElectStatistic &elect_statistic, 
        uint64_t max_tm_height);


    void addPrepareMembers2JoinStastics(
        shardora::common::MembersPtr &prepare_members,
        std::unordered_set<std::string> &added_id_set,
        shardora::pools::protobuf::ElectStatistic &elect_statistic,
        shardora::common::MembersPtr &now_elect_members);

    void addNewNode2JoinStatics(
        std::map<uint64_t, std::unordered_map<std::string, uint64_t>> &join_elect_stoke_map, 
        std::map<uint64_t, std::unordered_map<std::string, uint32_t>> &join_elect_shard_map, 
        std::unordered_set<std::string> &added_id_set, 
        std::unordered_map<std::string, std::string> &id_pk_map, 
        std::unordered_map<std::string, std::shared_ptr<elect::protobuf::BlsPublicKey>> &id_agg_bls_pk_map,
        std::unordered_map<std::string, std::shared_ptr<elect::protobuf::BlsPopProof>> &id_agg_bls_pk_proof_map,
        shardora::pools::protobuf::ElectStatistic &elect_statistic);

    void setElectStatistics(
        std::map<uint64_t, 
        std::unordered_map<std::string, shardora::pools::StatisticMemberInfoItem>>&,
        shardora::common::MembersPtr &now_elect_members, 
        shardora::pools::protobuf::ElectStatistic &elect_statistic,
        bool is_root);
    void CreateStatisticTransaction(uint64_t timeblock_height);
    // void HandleStatisticBlock(const block::protobuf::Block &block);
    void HandleStatistic(const std::shared_ptr<view_block::protobuf::ViewBlockItem> &block_ptr);
    std::string getLeaderIdFromBlock(const view_block::protobuf::ViewBlockItem &block);
    bool LoadAndStatisticBlock(uint32_t poll_index, uint64_t height);
    void cleanUpBlocks(PoolBlocksInfo& pool_blocks_info);
    void ThreadToStatistic(const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr);
    void ThreadCallback();

    static const uint32_t kLofRation = 5;
    static const uint32_t kLofMaxNodes = 8;
    static const uint32_t kLofValidMaxAvgTxCount = 1024u;

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint64_t latest_timeblock_height_ = 0;
    uint64_t prev_timeblock_height_ = 0;
    uint64_t pool_max_heihgts_[common::kInvalidPoolIndex] = { 0 };
    std::shared_ptr<PoolBlocksInfo> pools_consensus_blocks_[common::kInvalidPoolIndex];
    std::unordered_set<uint64_t> added_heights_[common::kInvalidPoolIndex];
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::atomic<uint64_t> now_elect_height_ = 0;
    std::atomic<uint64_t> prepare_elect_height_ = 0;
    std::shared_ptr<security::Security> secptr_ = nullptr;
    common::Tick tick_to_statistic_;
    std::unordered_map<std::string, std::shared_ptr<AccoutPoceInfoItem>> accout_poce_info_map_;
    uint64_t least_elect_height_for_statistic_=0;
    std::shared_ptr<pools::protobuf::StatisticTxItem> latest_statistic_item_ = nullptr;

    std::map<uint64_t, std::map<uint32_t, StatisticInfoItem>> statistic_pool_info_;
    uint64_t latest_statisticed_height_ = 0;
    std::map<uint64_t, pools::protobuf::ElectStatistic> statistic_height_map_;

    std::shared_ptr<std::thread> handle_block_thread_;
    common::ThreadSafeQueue<std::shared_ptr<view_block::protobuf::ViewBlockItem>> view_block_queue_;
    std::condition_variable thread_wait_conn_;
    std::mutex thread_wait_mutex_;
    std::atomic<bool> destroy_ = false;
    std::map<uint64_t, std::map<uint32_t, uint64_t>> pool_statistic_height_with_block_height_map_;

    DISALLOW_COPY_AND_ASSIGN(ShardStatistic);
};

}  // namespace pools

}  // namespace shardora
