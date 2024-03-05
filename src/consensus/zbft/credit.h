#pragma once

#include <map>
#include <unordered_set>

#include "common/global_info.h"
#include "common/log.h"
#include "common/node_members.h"
#include "common/thread_safe_queue.h"
#include "common/tick.h"
#include "common/time_utils.h"
#include "common/utils.h"
#include "consensus/zbft/zbft.h"
#include "db/db.h"
#include "protos/block.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace consensus {

class Credit {
public:
    Credit();
    ~Credit();
    void StatisticLeaderCredit(
        std::shared_ptr<ElectItem>& prev_elect_items,
        uint64_t next_elect_height);
    void ConsensusCommitBlock(
        ZbftPtr& bft_ptr,
        const std::shared_ptr<block::BlockToDbItem>& queue_item_ptr);
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    void OnTimeBlock(
        uint64_t tm_block_tm,
        uint64_t tm_height,
        uint64_t epoch_random);
    void OnNewElectBlock(
        uint64_t block_tm_ms,
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const libff::alt_bn128_G2& common_pk,
        const libff::alt_bn128_Fr& sec_key);
                
private:
    bool IsFirstPeriodsHandleMessage();
    bool IsSecondPeriodsHandleMessage();
    void BroadcastFirstPeriodHash(uint8_t thread_idx);
    void BroadcastSecondPeriodCredit(uint8_t thread_idx);
    void StatisticCredit(uint8_t thread_idx);

    int64_t kCreditPeriodUs = common::kTimeBlockCreatePeriodSeconds / 10 * 1000u * 1000u;
    static const uint64_t kCheckCreditPeriodUs = 10000000lu;

    struct CreditItem {
        uint64_t sum_gas;
    };

    struct CreditInfo {
        CreditInfo() : elect_height(0) {}
        CreditItem leader_credit[common::kEachShardMaxNodeCount];
        uint64_t elect_height;
        std::unordered_set<uint64_t> heights;
    };

    std::map<uint64_t, std::shared_ptr<CreditInfo>, std::less<uint64_t>> credits_;

    uint64_t first_offset_ = 0;
    uint64_t second_offset_ = 0;
    uint64_t epoch_random_ = 0;
    uint64_t latest_tm_block_tm_ = 0;
    uint64_t prev_tm_height_ = 0;
    uint64_t begin_time_us_ = 0;
    uint64_t end_tm_ = 0;
    uint64_t now_elect_height_ = common::kInvalidUint64;
    common::Tick statistic_credit_tick_;

    DISALLOW_COPY_AND_ASSIGN(Credit);
};

}  // namespace consensus

}  //namespace zjchain
