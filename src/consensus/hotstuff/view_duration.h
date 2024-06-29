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

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime).count();
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

        double duration = mean + dev * conf;
        if (max > 0 && duration > max) {
            duration = max;
        }

        // ZJC_DEBUG("pool: %d duration is %.2f ms", pool_idx_, duration);
        return static_cast<uint64_t>(duration) * 1000; // to us
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
