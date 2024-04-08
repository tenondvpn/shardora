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
            std::shared_ptr<pools::TxPoolManager>& pools_mgr)
            : elect_mgr_(elect_mgr), secptr_(sec_ptr), pools_mgr_(pools_mgr) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    ~ShardStatistic() {}
    void Init();
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
        pools::protobuf::ElectStatistic& elect_statistic,
        pools::protobuf::CrossShardStatistic& cross_statistic,
        uint64_t* statisticed_timeblock_height);

private:
    void CreateStatisticTransaction(uint64_t timeblock_height);
    void HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void HandleStatistic(const std::shared_ptr<block::protobuf::Block>& block_ptr);
    void HandleCrossShard(
        bool is_root,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        pools::protobuf::CrossShardStatistic& cross_statistic);
    void LoadLatestHeights();
    bool LoadAndStatisticBlock(uint32_t poll_index, uint64_t height);
    bool CheckAllBlockStatisticed(uint32_t local_net_id);
    void SetCanStastisticTx() {
        new_block_changed_ = true;
    }

    static const uint32_t kLofRation = 5;
    static const uint32_t kLofMaxNodes = 8;
    static const uint32_t kLofValidMaxAvgTxCount = 1024u;

    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint64_t latest_timeblock_height_ = 0;
    uint64_t prev_timeblock_height_ = 0;
    uint64_t pool_max_heihgts_[common::kInvalidPoolIndex] = { 0 };
    std::map<uint64_t, std::shared_ptr<HeightStatisticInfo>> node_height_count_map_[common::kInvalidPoolIndex];
    std::shared_ptr<PoolBlocksInfo> pools_consensus_blocks_[common::kInvalidPoolIndex];
    std::unordered_map<uint32_t, std::shared_ptr<common::Point>> point_ptr_map_;
    std::shared_ptr<pools::protobuf::StatisticTxItem> tx_heights_ptr_ = nullptr;
    std::unordered_set<uint64_t> added_heights_[common::kInvalidPoolIndex];
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint64_t prev_elect_height_ = 0;
    uint64_t now_elect_height_ = 0;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    uint64_t prepare_elect_height_ = 0;
    std::shared_ptr<security::Security> secptr_ = nullptr;
    volatile bool new_block_changed_ = false;
    uint64_t statisticed_timeblock_height_ = 0;
    common::Tick tick_to_statistic_;

    DISALLOW_COPY_AND_ASSIGN(ShardStatistic);
};

}  // namespace pools

}  // namespace shardora
