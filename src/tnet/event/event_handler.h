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
        if (should_stop_timeout_ms_ > 0 && now_ms >= should_stop_timeout_ms_ + 120000lu) {
            stoped_ = true;
        }

        return stoped_;
    }

    void ShouldStop() {
        should_stop_ = true;
        should_stop_timeout_ms_ = common::TimeUtils::TimestampMs();
    }

    void Stop() {
        SHARDORA_DEBUG("network socket stopted.");
        stoped_ = true;
    }

private:
    
    int32_t event_type_{ 0 };
    std::atomic<bool> should_stop_ = false;
    std::atomic<bool> stoped_ = false;
    uint64_t should_stop_timeout_ms_ = 0;

};

}  // namespace tnet

}  // namespace shardora
