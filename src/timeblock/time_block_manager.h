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
#include "protos/zbft.pb.h"
#include "protos/prefix_db.h"
#include "protos/timeblock.pb.h"
#include "protos/transport.pb.h"

namespace zjchain {

namespace vss {
    class VssManager;
}

namespace timeblock {

class TimeBlockManager {
public:
    TimeBlockManager();
    ~TimeBlockManager();
    void Init(std::shared_ptr<vss::VssManager>& vss_mgr);
    uint64_t LatestTimestamp();
    uint64_t LatestTimestampHeight();
    void OnTimeBlock(
        uint64_t lastest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random);
    pools::TxItemPtr tmblock_tx_ptr(bool leader);

    void SetCreateTmTxFunction(pools::CreateConsensusItemFunction func) {
        create_tm_tx_cb_ = func;
    }

private:
    void CreateTimeBlockTx();

    bool CanCallTimeBlockTx() const {
        uint64_t now_sec = common::TimeUtils::TimestampSeconds();
        if (now_sec >= latest_time_block_tm_ + common::kTimeBlockCreatePeriodSeconds) {
            return true;
        }

        if (now_sec >= latest_tm_block_local_sec_ + common::kTimeBlockCreatePeriodSeconds) {
            return true;
        }

        return false;
    }


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
    pools::TxItemPtr tmblock_tx_ptr_ = nullptr;
    pools::CreateConsensusItemFunction create_tm_tx_cb_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;

    DISALLOW_COPY_AND_ASSIGN(TimeBlockManager);
};

}  // namespace timeblock

}  // namespace zjchain
