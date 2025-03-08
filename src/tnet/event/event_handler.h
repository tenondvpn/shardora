#pragma once

#include <cstdint>

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

    bool CheckStoped() const {
        return stoped_;
    }

    void ShouldStop() {
        should_stop_ = true;
    }

    void Stop() {
        ZJC_DEBUG("network socket stopted.");
        stoped_ = true;
    }

private:
    int32_t event_type_{ 0 };
    volatile bool should_stop_ = false;
    volatile bool stoped_ = false;

};

}  // namespace tnet

}  // namespace shardora
