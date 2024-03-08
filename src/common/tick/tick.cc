#include "common/tick.h"

#include <cassert>

#include "common/global_info.h"

namespace zjchain {

namespace common {

Tick::Tick() : tick_index_(TickThreadPool::Instance()->TickIndex()) {}

Tick::~Tick() {
    Destroy();
}

void Tick::CutOff(int64_t cutoff_us, TickCallbackFunction call) {
    TickThreadPool::Instance()->AddTick(tick_index_, cutoff_us, call);
}

void Tick::Destroy() {
    TickThreadPool::Instance()->RemoveTick(tick_index_);
}

}  // namespace common

}  // namespace zjchain
