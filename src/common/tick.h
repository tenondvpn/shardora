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

}  // namespace common

}  // namespace zjchain
