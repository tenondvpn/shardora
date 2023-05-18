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

namespace zjchain {

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
    void OnNewBlock(const block::protobuf::Block& block);
    void GetStatisticInfo(
        uint64_t timeblock_height,
        block::protobuf::StatisticInfo* statistic_info);
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    int StatisticWithHeights(
        const pools::protobuf::ToTxHeights& leader_to_heights,
        std::string* statistic_hash,
        std::string* cross_hash);
    int LeaderCreateStatisticHeights(pools::protobuf::ToTxHeights& to_heights);

private:
    void CreateStatisticTransaction(uint64_t timeblock_height);
    void NormalizePoints(
        uint64_t elect_height,
        std::unordered_map<int32_t, std::shared_ptr<common::Point>>& leader_lof_map);
    void HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void HandleStatistic(const block::protobuf::Block& block);
    void HandleCrossShard(
        bool is_root,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void LoadLatestHeights();
    void NormalizeLofMap(std::unordered_map<uint32_t, common::Point>& lof_map);

    static const uint32_t kLofRation = 5;
    static const uint32_t kLofMaxNodes = 8;
    static const uint32_t kLofValidMaxAvgTxCount = 1024u;

    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint64_t latest_timeblock_tm_ = 0;
    uint64_t pool_max_heihgts_[common::kInvalidPoolIndex] = { 0 };
    uint64_t pool_consensus_heihgts_[common::kInvalidPoolIndex] = { 0 };
    std::map<uint64_t, std::shared_ptr<HeightStatisticInfo>> node_height_count_map_[common::kInvalidPoolIndex];
    std::map<uint64_t, uint32_t> cross_shard_map_[common::kInvalidPoolIndex];
    std::unordered_map<uint32_t, std::shared_ptr<common::Point>> point_ptr_map_;
    std::shared_ptr<pools::protobuf::ToTxHeights> tx_heights_ptr_ = nullptr;
    std::unordered_set<uint64_t> added_heights_[common::kInvalidPoolIndex];
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint64_t prev_elect_height_ = 0;
    uint64_t now_elect_height_ = 0;
    uint64_t now_vss_random_ = 0;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    uint64_t prepare_elect_height_ = 0;
    std::shared_ptr<security::Security> secptr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(ShardStatistic);
};

}  // namespace pools

}  // namespace zjchain
