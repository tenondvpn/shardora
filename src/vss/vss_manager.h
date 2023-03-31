#pragma once

#include <mutex>
#include <atomic>

#include "common/utils.h"
#include "common/tick.h"
#include "protos/vss.pb.h"
#include "protos/transport.pb.h"
#include "security/security.h"
#include "transport/transport_utils.h"
#include "vss/random_num.h"

namespace zjchain {

namespace vss {

class VssManager {
public:
    VssManager(std::shared_ptr<security::Security>& security_ptr);
    ~VssManager() {}
    void OnTimeBlock(
        uint64_t tm_block_tm,
        uint64_t tm_height,
        uint64_t epoch_random);
    void OnNewElectBlock(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members);
    uint64_t EpochRandom();
    uint64_t GetConsensusFinalRandom();

    void SetFinalVss(uint64_t vss_random) {
        epoch_random_ = vss_random;
    }
   
private:
    // just two period and consensus with time block can also guarantee safety
    void ClearAll();
    bool IsVssFirstPeriodsHandleMessage();
    bool IsVssSecondPeriodsHandleMessage();
    bool IsVssThirdPeriodsHandleMessage();
    void BroadcastFirstPeriodHash(uint8_t thread_idx);
    void BroadcastSecondPeriodRandom(uint8_t thread_idx);
    void BroadcastThirdPeriodRandom(uint8_t thread_idx);
    void HandleFirstPeriodHash(const protobuf::VssMessage& vss_msg);
    void HandleSecondPeriodRandom(const protobuf::VssMessage& vss_msg);
    void HandleThirdPeriodRandom(const protobuf::VssMessage& vss_msg);
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    uint64_t GetAllVssValid();
    void SetConsensusFinalRandomNum(const std::string& id, uint64_t final_random_num);
    void ConsensusTimerMessage(const transport::MessagePtr& msg_ptr);

    int64_t kDkgPeriodUs = common::kTimeBlockCreatePeriodSeconds / 10 * 1000u * 1000u;

    RandomNum local_random_{ true };
    RandomNum other_randoms_[common::kEachShardMaxNodeCount];
    uint64_t prev_tm_height_{ common::kInvalidUint64 };
    uint64_t prev_elect_height_{ 0 };
    uint64_t latest_tm_block_tm_{ 0 };
    uint64_t prev_epoch_final_random_{ 0 };
    bool first_period_cheched_{ false };
    bool second_period_cheched_{ false };
    bool third_period_cheched_{ false };
    uint64_t epoch_random_{ 0 };
    std::unordered_set<std::string> final_consensus_nodes_;
    std::unordered_map<uint64_t, uint32_t> final_consensus_random_count_;
    uint32_t max_count_{ 0 };
    uint64_t max_count_random_{ 0 };
    uint64_t prev_max_count_random_{ 0 };
    int64_t begin_time_us_{ 0 };
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    ElectItem elect_item_[2];
    uint32_t elect_valid_index_ = 0;

    uint64_t first_offset_ = 0;
    uint64_t second_offset_ = 0;
    uint64_t third_offset_ = 0;
    uint64_t first_try_times_ = 0;
    uint64_t first_prev_tm_ = 0;
    uint64_t second_try_times_ = 0;
    uint64_t second_prev_tm_ = 0;
    uint64_t third_try_times_ = 0;
    uint64_t third_prev_tm_ = 0;
    uint64_t end_tm_  = 0;

    // for unit test
#ifdef ZJC_UNITTEST
    transport::protobuf::Header first_msg_;
    transport::protobuf::Header second_msg_;
    transport::protobuf::Header third_msg_;
#endif

    DISALLOW_COPY_AND_ASSIGN(VssManager);
};

}  // namespace vss

}  // namespace zjchain
