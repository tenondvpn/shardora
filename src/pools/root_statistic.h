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
#include "protos/prefix_db.h"

namespace zjchain {

namespace elect {
    class ElectManager;
};

namespace pools {

class TxPoolManager;
class RootStatistic {
public:
    RootStatistic(
            std::shared_ptr<elect::ElectManager>& elect_mgr,
            std::shared_ptr<db::Db>& db,
            std::shared_ptr<pools::TxPoolManager>& pools_mgr)
            : elect_mgr_(elect_mgr), pools_mgr_(pools_mgr) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db);
    }

    ~RootStatistic() {}
    void Init();
    void OnNewElectBlock(uint32_t sharding_id, uint64_t elect_height);
    void OnNewBlock(const block::protobuf::Block& block);
    void GetStatisticInfo(
        uint64_t timeblock_height,
        block::protobuf::StatisticInfo* statistic_info);
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);

private:
    void CreateStatisticTransaction(uint64_t timeblock_height);
    void LoadLatestHeights();
    void HandleStatisticBlock(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx);

    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint64_t latest_timeblock_tm_ = 0;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::unordered_map<uint32_t, std::set<uint64_t>> handled_sharding_statistic_map_;
    std::unordered_map<uint32_t, uint64_t> latest_elect_height_map_ = 0;
    std::unordered_map<std::string, RootStatisticItem> node_tx_count_map_;

    DISALLOW_COPY_AND_ASSIGN(RootStatistic);
};

}  // namespace pools

}  // namespace zjchain
