#include "consensus/zbft/credit.h"

namespace zjchain {

namespace consensus {

Credit::Credit() {
}

Credit::~Credit() {}

void Credit::StatisticLeaderCredit(
        std::shared_ptr<ElectItem>& prev_elect_items,
        uint64_t next_elect_height) {
    if (prev_elect_items->elect_height != now_elect_height_) {
        return;
    }

    if (next_elect_height <= now_elect_height_) {
        return;
    }

    if (now_elect_height_ != common::kInvalidUint64) {
        auto iter = credits_.find(now_elect_height_);
        if (iter != credits_.end()) {
            for (uint32_t i = 0; i < prev_elect_items->member_size; ++i) {

            }
        }
    }

    now_elect_height_ = next_elect_height;
}

void OnNewElectBlock(
        uint64_t block_tm_ms,
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members,
        const libff::alt_bn128_G2& common_pk,
        const libff::alt_bn128_Fr& sec_key) {
    
}

void Credit::OnTimeBlock(
        uint64_t tm_block_tm,
        uint64_t tm_height,
        uint64_t epoch_random) {
    epoch_random_ = epoch_random;
    latest_tm_block_tm_ = tm_block_tm;
    prev_tm_height_ = tm_height;
    int64_t local_offset_us = 0;
    auto tmblock_tm = tm_block_tm * 1000l * 1000l;
    auto offset_tm = 30l * 1000l * 1000l;
    kCreditPeriodUs = common::kTimeBlockCreatePeriodSeconds / 10 * 1000u * 1000u;
    if (tmblock_tm + kCreditPeriodUs + offset_tm < common::TimeUtils::TimestampUs()) {
        // ignore
        ZJC_ERROR("may be local time invalid, tmblock tm: %lu, "
            "tmblock_tm + kDkgPeriodUs + offset_tm: %lu, local tm: %lu",
            tmblock_tm, kCreditPeriodUs, offset_tm,
            (tmblock_tm + kCreditPeriodUs + offset_tm),
            common::TimeUtils::TimestampUs());
        return;
    }

    begin_time_us_ = common::TimeUtils::TimestampUs();
    first_offset_ = tmblock_tm + kCreditPeriodUs;
    second_offset_ = tmblock_tm + kCreditPeriodUs * 4;
    if (begin_time_us_ < (int64_t)tmblock_tm + offset_tm) {
        kCreditPeriodUs = (common::kTimeBlockCreatePeriodSeconds - 20) * 1000l * 1000l / 10l;
        first_offset_ = tmblock_tm + offset_tm;
        begin_time_us_ = tmblock_tm + offset_tm - kCreditPeriodUs;
        second_offset_ = first_offset_ + kCreditPeriodUs * 3;
    }

    end_tm_ = tmblock_tm + common::kTimeBlockCreatePeriodSeconds * 1000000lu;
    ZJC_DEBUG("tmblock_tm: %lu, begin_time_us_: %lu, first_offset_: %lu, second_offset_: %lu, kDkgPeriodUs: %lu",
        tmblock_tm, begin_time_us_, first_offset_, second_offset_, kCreditPeriodUs);
}

void Credit::ConsensusCommitBlock(
        ZbftPtr& bft_ptr,
        const std::shared_ptr<block::BlockToDbItem>& queue_item_ptr) {
    auto consensus_use_tm_ms = bft_ptr->get_consensus_use_tm_ms();
    std::shared_ptr<CreditInfo> credit_ptr = nullptr;
    auto iter = credits_.find(bft_ptr->elect_height());
    if (!credits_.empty()) {
        auto begin_iter = credits_.begin();
        if (begin_iter != credits_.end()) {
            if (bft_ptr->elect_height() < begin_iter->first) { 
                return;
            }
        }
    }
    
    if (iter == credits_.end()) {
        credit_ptr = std::make_shared<CreditInfo>();
        credits_[bft_ptr->elect_height()] = credit_ptr;
        if (credits_.size() > 3) {
            credits_.erase(credits_.begin());
        }
    } else {
        credit_ptr = iter->second;
    }

    if (consensus_use_tm_ms > 0) {
        credit_ptr->leader_credit[bft_ptr->leader_index()].sum_gas += queue_item_ptr->block_ptr->tx_list_size();
    }
}

int Credit::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void Credit::StatisticCredit(uint8_t thread_idx) {
    statistic_credit_tick_.CutOff(
        kCheckCreditPeriodUs,
        std::bind(&Credit::StatisticCredit, this, std::placeholders::_1));
}

}  // namespace consensus

}  //namespace zjchain
