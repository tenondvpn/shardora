#pragma once

#include <chrono>

namespace shardora {

namespace common {

class FlowControl {
public:
    // 构造函数接受间隔秒数，而不是每秒请求数
    FlowControl(double intervalInSeconds)
        : interval(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(intervalInSeconds))),
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
