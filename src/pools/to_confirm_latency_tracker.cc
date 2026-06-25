#include "pools/to_confirm_latency_tracker.h"

#include <algorithm>
#include <cstring>

#include "common/encode.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "protos/pools.pb.h"

namespace shardora {

namespace pools {

std::string ToConfirmLatencyTracker::NormalizeToKey(const std::string& address) {
    if (address.empty()) {
        return {};
    }

    if (address.size() >= common::kUnicastAddressLength) {
        return address.substr(0, common::kUnicastAddressLength);
    }

    return address;
}

void ToLatencyEvent::SetTo(const std::string& address) {
    to_len = static_cast<uint8_t>(
        std::min(address.size(), static_cast<size_t>(common::kUnicastAddressLength)));
    if (to_len > 0) {
        std::memcpy(to, address.data(), to_len);
    }
}

std::string ToLatencyEvent::ToString() const {
    return std::string(to, to_len);
}

void ToConfirmLatencyTracker::EnqueueEvent(
        ToLatencyEvent::Type type,
        const std::string& key,
        uint64_t timestamp_us) {
    if (key.empty()) {
        return;
    }

    ToLatencyEvent event;
    event.type = type;
    event.timestamp_us = timestamp_us > 0 ? timestamp_us : common::TimeUtils::TimestampUs();
    event.SetTo(key);

    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    if (thread_idx >= common::kMaxThreadCount) {
        thread_idx = static_cast<uint8_t>(thread_idx % common::kMaxThreadCount);
    }

    event_queues_[thread_idx].push(event);
}

void ToConfirmLatencyTracker::OnStart(const std::string& des, uint64_t timestamp_us) {
    EnqueueEvent(ToLatencyEvent::Type::kStart, NormalizeToKey(des), timestamp_us);
}

void ToConfirmLatencyTracker::OnConfirmFromLocalToTx(
        const std::string& to,
        const std::string& tx_value) {
    std::string key = NormalizeToKey(to);
    if (!tx_value.empty()) {
        pools::protobuf::ToTxMessageItem item;
        if (item.ParseFromString(tx_value) && !item.des().empty()) {
            key = NormalizeToKey(item.des());
        }
    }

    EnqueueEvent(ToLatencyEvent::Type::kConfirm, key, 0);
}

void ToConfirmLatencyTracker::OnAddTx(
        int32_t step,
        const std::string& to,
        const std::string& tx_value) {
    if (to.empty()) {
        return;
    }

    if (step == pools::protobuf::kNormalFrom) {
        OnStart(to);
    } else if (step == pools::protobuf::kConsensusLocalTos) {
        OnConfirmFromLocalToTx(to, tx_value);
    }
}

void ToConfirmLatencyTracker::ProcessEvents() {
    DrainQueues();
    MaybeReportAverage();
}

void ToConfirmLatencyTracker::DrainQueues() {
    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        ToLatencyEvent event;
        while (event_queues_[i].pop(&event)) {
            HandleEvent(event);
        }
    }
}

void ToConfirmLatencyTracker::HandleEvent(const ToLatencyEvent& event) {
    const std::string to = event.ToString();
    if (to.empty()) {
        return;
    }

    if (event.type == ToLatencyEvent::Type::kStart) {
        auto& starts = pending_starts_[to];
        if (starts.size() >= kMaxPendingStartsPerKey) {
            starts.pop_front();
        }
        starts.push_back(event.timestamp_us);
        return;
    }

    auto iter = pending_starts_.find(to);
    if (iter == pending_starts_.end() || iter->second.empty()) {
        return;
    }

    const uint64_t start_us = iter->second.front();
    iter->second.pop_front();
    if (iter->second.empty()) {
        pending_starts_.erase(iter);
    }

    if (event.timestamp_us <= start_us) {
        return;
    }

    const uint64_t latency_us = event.timestamp_us - start_us;
    latency_sum_us_ += latency_us;
    ++latency_count_;
    SHARDORA_DEBUG("[ToConfirmLatency] average delay des=%s latency=%lu us",
        common::Encode::HexEncode(to).c_str(),
        latency_us);
}

void ToConfirmLatencyTracker::MaybeReportAverage() {
    const uint64_t now_us = common::TimeUtils::TimestampUs();
    if (last_report_us_ != 0 && now_us - last_report_us_ < kReportIntervalUs) {
        return;
    }

    const uint64_t avg = latency_count_ > 0 ? latency_sum_us_ / latency_count_ : 0;
    avg_latency_us_.store(avg, std::memory_order_relaxed);
    last_report_count_.store(latency_count_, std::memory_order_relaxed);
    SHARDORA_WARN("[ToConfirmLatency] average delay to_tx avg=%lu us, count=%lu, interval=%lu us",
        avg,
        latency_count_,
        last_report_us_ == 0 ? 0llu : (now_us - last_report_us_));

    latency_sum_us_ = 0;
    latency_count_ = 0;
    last_report_us_ = now_us;
}

}  // namespace pools

}  // namespace shardora
