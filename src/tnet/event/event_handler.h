#pragma once

#include <cstdint>

#include "common/time_utils.h"

namespace shardora {

namespace tnet {

class EventHandler {
public:
    EventHandler() {}

    virtual ~EventHandler() {}

    virtual bool OnRead() = 0;
    virtual void OnWrite() = 0;

    int32_t event_type() const {
        return event_type_;
    }

    void set_event_type(int32_t type) {
        event_type_ = type;
    }

    bool Valid() const {
        return !should_stop_ && !stoped_;
    }

    bool CheckShouldStop() const {
        return should_stop_;
    }

    bool CheckStoped() {
        if (stoped_) {
            return stoped_;
        }

        auto now_ms = common::TimeUtils::TimestampMs();
        auto timeout_ms = should_stop_timeout_ms_.load(std::memory_order_relaxed);
        if (timeout_ms > 0 && now_ms >= timeout_ms + 120000lu) {
            stoped_ = true;
        }

        return stoped_;
    }

    void ShouldStop() {
        should_stop_ = true;
        should_stop_timeout_ms_.store(common::TimeUtils::TimestampMs(), std::memory_order_relaxed);
    }

    void Stop() {
        SHARDORA_DEBUG("network socket stopted.");
        stoped_ = true;
    }

private:
    
    // Bug fix #20: event_type_ accessed from epoll thread and IO threads without
    // synchronization. Made atomic to prevent torn reads/writes.
    std::atomic<int32_t> event_type_{ 0 };
    std::atomic<bool> should_stop_ = false;
    std::atomic<bool> stoped_ = false;
    // Bug fix #21: should_stop_timeout_ms_ was a plain uint64_t written by one
    // thread and read by another (CheckStoped). Made atomic.
    std::atomic<uint64_t> should_stop_timeout_ms_{ 0 };

};

}  // namespace tnet

}  // namespace shardora
