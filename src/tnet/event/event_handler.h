#pragma once

#include <cstdint>

namespace zjchain {

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

private:
    int32_t event_type_{ 0 };
};

}  // namespace tnet

}  // namespace zjchain
