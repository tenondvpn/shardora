#pragma once

#include "tnet/event/event_handler.h"

namespace shardora {

namespace tnet {

enum IoEventType {
    kEventRead = 1,
    kEventWrite = 2
};

class IoEvent {
public:
    IoEvent() {}

    ~IoEvent() {}

    EventHandler* GetHandler() const {
        return event_handler_;
    }

    void SetHandler(EventHandler* handler) {
        event_handler_ = handler;
    }

    int GetType() const {
        return type_;
    }

    void SetType(int type) {
        type_ = type;
    }

    void Process() const {
        if (event_handler_ == NULL) {
            return;
        }

        
        if (!event_handler_->Valid()) {
            event_handler_->Stoped();
            return;
        }
        
        bool rc = true;
        if ((type_ & kEventRead) == kEventRead) {
            rc = event_handler_->OnRead();
        }

        if (rc && (type_ & kEventWrite) == kEventWrite) {
            event_handler_->OnWrite();
        }

        if (event_handler_->CheckShouldStop()) {
            event_handler_->Stoped();
        }
    }

    void Reset() {
        type_ = 0;
        event_handler_ = nullptr;
    }

private:
    EventHandler* event_handler_{ nullptr };
    int32_t type_{ 0 };
};

}  // namespace tnet

}  // namespace shardora
