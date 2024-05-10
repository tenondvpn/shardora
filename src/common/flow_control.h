#pragma once

#include <chrono>

namespace shardora {

namespace common {

class FlowControl {
public:
    FlowControl(int maxRequestsPerSecond)
        : interval(std::chrono::seconds(1) / maxRequestsPerSecond),
          lastRequest(std::chrono::steady_clock::now() - interval) {
    }

    bool Permitted() {
        auto now = std::chrono::steady_clock::now();
        if (now - lastRequest < interval) {
            return false;
        } else {
            lastRequest = now;
            return true;
        }
    }

private:
    std::chrono::steady_clock::duration interval;
    std::chrono::steady_clock::time_point lastRequest;
};

}

} // namespace shardora
