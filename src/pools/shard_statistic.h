#pragma once

#include <unordered_map>
#include <atomic>
#include <random>
#include <memory>

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
    }

    ~ShardStatistic() {}
    void Init(const std::vector<uint64_t>& latest_heights);
    uint64_t getStoke(uint32_t shard_id, std::string contractId, std::string temp_addr, uint64_t elect_height);
    void OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t prepare_elect_height,
        uint64_t elect_height);
    void OnNewBlock(const std::shared_ptr<block::protobuf::Block>& block);
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    int StatisticWithHeights(
        pools::protobuf::ElectStatistic &elect_statistic,
        uint64_t statisticed_timeblock_height);


  private:
    uint64_t getStoke(uint32_t shard_id, std::string addr) {
        return 11;
    };
    void addHeightInfo2Statics(shardora::pools::protobuf::ElectStatistic &elect_statistic, uint64_t max_tm_height);


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
        std::unordered_map<std::string, std::string> &id_agg_bls_pk_map,
        shardora::pools::protobuf::ElectStatistic &elect_statistic);

    void setElectStatistics(
        std::map<uint64_t, 
        std::unordered_map<std::string, shardora::pools::StatisticMemberInfoItem>>&,
        shardora::common::MembersPtr &now_elect_members, 
        shardora::pools::protobuf::ElectStatistic &elect_statistic,
        bool is_root);
    void CreateStatisticTransaction(uint64_t timeblock_height);
    void HandleStatisticBlock(
        const block::protobuf::Block &block,
        const block::protobuf::BlockTx &tx);
    void HandleStatistic(const std::shared_ptr<block::protobuf::Block> &block_ptr);
    std::string getLeaderIdFromBlock(shardora::block::protobuf::Block &block);
    bool LoadAndStatisticBlock(uint32_t poll_index, uint64_t height);
    bool CheckAllBlockStatisticed(uint32_t local_net_id);
    void cleanUpBlocks(PoolBlocksInfo& pool_blocks_info);
    bool checkBlockValid(shardora::block::protobuf::Block &block);

    bool IsShardReachPerformanceLimit(
            std::shared_ptr<StatisticInfoItem>& statistic_info_ptr,
            const block::protobuf::Block& block);

    static const uint32_t kLofRation = 5;
    static const uint32_t kLofMaxNodes = 8;
    static const uint32_t kLofValidMaxAvgTxCount = 1024u;

    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint64_t latest_timeblock_height_ = 0;
    uint64_t prev_timeblock_height_ = 0;
    uint64_t pool_max_heihgts_[common::kInvalidPoolIndex] = { 0 };
    std::map<uint64_t, std::shared_ptr<HeightStatisticInfo>> node_height_count_map_[common::kInvalidPoolIndex];
    std::shared_ptr<PoolBlocksInfo> pools_consensus_blocks_[common::kInvalidPoolIndex];
    std::unordered_map<uint32_t, std::shared_ptr<common::Point>> point_ptr_map_;
    std::unordered_set<uint64_t> added_heights_[common::kInvalidPoolIndex];
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint64_t prev_elect_height_ = 0;
    uint64_t now_elect_height_ = 0;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    uint64_t prepare_elect_height_ = 0;
    std::shared_ptr<security::Security> secptr_ = nullptr;
    common::Tick tick_to_statistic_;
    std::unordered_map<std::string, std::shared_ptr<AccoutPoceInfoItem>> accout_poce_info_map_;
    uint64_t least_elect_height_for_statistic_=0;
    std::unordered_map<uint64_t, std::shared_ptr<StatisticInfoItem>> tm_height_with_statistic_info_;
    common::Bitmap shard_pref_bitmap_{(common::kPreopenShardMaxBlockWindowSize/64+1)*64}; // bit 窗口，保存了最近多个 block 是否达到 shard 性能处理上限
    
    DISALLOW_COPY_AND_ASSIGN(ShardStatistic);
};

}  // namespace pools

}  // namespace shardora
