#pragma once
#include <chrono>
#include <cmath>
#include <common/log.h>

namespace shardora {

namespace hotstuff {

class ViewDuration {
public:
    // sampleSize determines the number of previous views that should be considered.
    // startTimeout determines the view duration of the first views.
    // When a timeout occurs, the next view duration will be multiplied by the multiplier.    
    ViewDuration(
            const uint32_t& pool_idx,
            const uint64_t& sampleSize,
            const double& startTimeout,
            const double& maxTimeout,
            const double& multiplier)
        : pool_idx_(pool_idx), mul(multiplier),
          limit(sampleSize), mean(startTimeout), max(maxTimeout) {}

    ~ViewDuration() {};

    ViewDuration(const ViewDuration&) = delete;
    ViewDuration& operator=(const ViewDuration&) = delete;

    void ViewStarted() {
        startTime = std::chrono::high_resolution_clock::now();
    }

    void ViewSucceeded() {
        if (startTime.time_since_epoch().count() == 0) {
            return;
        }

        const auto now = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(now - startTime).count();
        // Integer ms truncates to 0 on same-millisecond samples and collapses `mean` to 0 on
        // the first Welford step; sub-ms resolution plus a tiny floor covers same-tick pairs.
        if (duration <= 0.0) {
            duration = 0.001; // 1µs expressed in ms
        }
        count++;

        if (count % limit == 0) {
            prevM2 = m2;
            m2 = 0;
        }

        double c;
        if (count > limit) {
            c = limit;
            mean -= mean / c;
        } else {
            c = count;
        }

        double d1 = duration - mean;
        mean += d1 / c;
        double d2 = duration - mean;
        m2 += d1 * d2;
    }

    void ViewTimeout() {
        mean *= mul;
    }

    uint64_t Duration() const {
        // return 5000000;
        double conf = 1.96; // 95% confidence
        double dev = 0.0;
        if (count > 1) {
            double c = count;
            double m2_temp = m2;
            if (count >= limit) {
                c = limit + count % limit;
                m2_temp += prevM2;
            }
            dev = std::sqrt(m2_temp / c);
        }

        double duration_ms = mean + dev * conf;
        if (max > 0 && duration_ms > max) {
            duration_ms = max;
        }

        // SHARDORA_DEBUG("pool: %d duration is %.2f ms", pool_idx_, duration_ms);
        double us_d = std::max(0.0, duration_ms * 1000.0); // ms → µs
        uint64_t us = static_cast<uint64_t>(std::llround(us_d));
        // After at least one successful view, never report 0 µs (rounding / FP / pathological m2).
        if (count > 0 && us == 0) {
            us = 1;
        }
        return us;
    }

private:
    uint32_t pool_idx_;
    double mul;
    uint64_t limit;
    uint64_t count = 0;
    std::chrono::high_resolution_clock::time_point startTime;
    double mean;
    double m2 = 0.0;
    double prevM2 = 0.0;
    double max;
};

} // namespace hotstuff

} // namespace shardora
