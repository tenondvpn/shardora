#pragma once

#include <unordered_map>
#include <atomic>
#include <random>
#include <memory>

#include "common/utils.h"
#include "common/bitmap.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "pools/tx_utils.h"
#include "protos/block.pb.h"

namespace zjchain {

namespace elect {
    class ElectManager;
};

namespace pools {

class ShardStatistic {
public:
    ShardStatistic(std::shared_ptr<elect::ElectManager>& elect_mgr)
            : elect_mgr_(elect_mgr) {
        for (uint32_t i = 0; i < kStatisticMaxCount; ++i) {
            statistic_items_[i] = std::make_shared<StatisticItem>();
        }
    }

    ~ShardStatistic() {}
    void OnNewBlock(const std::shared_ptr<block::protobuf::Block>& block_item);
    void GetStatisticInfo(
        uint64_t timeblock_height,
        block::protobuf::StatisticInfo* statistic_info);
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    void StatisticWithHeights(const pools::protobuf::ToTxHeights& leader_to_heights);
    int LeaderCreateToHeights(pools::protobuf::ToTxHeights& to_heights);

private:
    void CreateStatisticTransaction(uint64_t timeblock_height);
    void NormalizePoints(
        uint64_t elect_height,
        std::unordered_map<int32_t, std::shared_ptr<common::Point>>& leader_lof_map);
    void HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);
    void HandleStatistic(const block::protobuf::Block& block);

    static const uint32_t kLofRation = 5;
    static const uint32_t kLofMaxNodes = 8;

    std::shared_ptr<StatisticItem> statistic_items_[kStatisticMaxCount];
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint64_t latest_timeblock_tm_ = 0;
    std::set<uint64_t> pool_heights_[common::kInvalidPoolIndex];
    uint64_t latest_heights_[common::kInvalidPoolIndex] = { 0 };
    uint64_t pool_max_heihgts_[common::kInvalidPoolIndex] = { 0 };
    uint64_t pool_consensus_heihgts_[common::kInvalidPoolIndex] = { 0 };
    std::unordered_map<uint64_t, std::shared_ptr<HeightStatisticInfo>> node_height_count_map_;
    std::unordered_map<uint32_t, std::shared_ptr<common::Point>> point_ptr_map_;
    std::shared_ptr<pools::protobuf::ToTxHeights> tx_heights_ptr_ = nullptr;
    std::unordered_set<uint64_t> added_heights_[common::kInvalidPoolIndex];

    DISALLOW_COPY_AND_ASSIGN(ShardStatistic);
};

}  // namespace pools

}  // namespace zjchain
