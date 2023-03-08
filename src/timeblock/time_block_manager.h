#pragma once

#include <atomic>
#include <mutex>
#include <deque>

#include "common/utils.h"
#include "common/time_utils.h"
#include "common/tick.h"
#include "db/db.h"
#include "dht/dht_utils.h"
#include "pools/tx_pool_manager.h"
#include "protos/hotstuff.pb.h"
#include "protos/prefix_db.h"
#include "protos/timeblock.pb.h"
#include "protos/transport.pb.h"

namespace zjchain {

namespace tmblock {

class TimeBlockManager {
public:
    TimeBlockManager();
    ~TimeBlockManager();
    void Init(
            std::shared_ptr<pools::TxPoolManager>& pools_mgr,
            std::shared_ptr<db::Db>& db) {
        pools_mgr_ = pools_mgr;
        db_ = db;
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        LoadLatestTimeBlock();
    }

    uint64_t LatestTimestamp();
    uint64_t LatestTimestampHeight();
    void UpdateTimeBlock(
        uint64_t latest_time_block_height,
        uint64_t lastest_time_block_tm,
        uint64_t vss_random);
//     bool LeaderNewTimeBlockValid(uint64_t* new_time_block_tm);
    bool BackupheckNewTimeBlockValid(uint64_t new_time_block_tm);
    int LeaderCreateTimeBlockTx(transport::protobuf::Header* msg);
    int BackupCheckTimeBlockTx(const pools::protobuf::TxMessage& tx_info);
    bool LeaderCanCallTimeBlockTx(uint64_t tm_sec);

private:
    void CreateTimeBlockTx();
    void CheckBft();
    void LoadLatestTimeBlock();

    uint64_t latest_time_block_height_ = common::kInvalidUint64;
    uint64_t latest_time_block_tm_{ 0 };
    std::mutex latest_time_blocks_mutex_;
    common::Tick check_bft_tick_;
    common::Tick broadcast_tm_tick_;
    uint64_t latest_tm_block_local_sec_{ 0 };
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<timeblock::protobuf::TimeBlock> timeblock_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(TimeBlockManager);
};

}  // namespace tmblock

}  // namespace zjchain
