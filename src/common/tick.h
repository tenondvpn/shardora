#pragma once

#include <list>
#include <mutex>

#include "common/spin_mutex.h"
#include "common/tick/thread_pool.h"
#include "common/utils.h"

namespace zjchain {

namespace common {

// Note: if class use tick, should use shared_from_this
class Tick {
public:
    Tick();
    ~Tick();

    void CutOff(int64_t cutoff_us, TickCallbackFunction call);
    void Destroy();
    uint32_t tick_index() {
        return tick_index_;
    }

private:
    uint32_t tick_index_{ 0 };

    DISALLOW_COPY_AND_ASSIGN(Tick);
};

class IndependentTick {
public:
    IndependentTick();
    ~IndependentTick();
    void CutOff(int64_t cutoff_us, TickCallbackFunction call);
    void Destroy();

private:
    typedef std::pair<std::chrono::steady_clock::time_point, TickCallbackFunction> TickItem;

    void TimerFlies();

    std::list<TickItem> tick_list_;
    common::SpinMutex timer_list_mutex_;
    std::shared_ptr<std::thread> timer_thread_;
    bool destroy_;
};
}  // namespace common

}  // namespace zjchain
