#pragma once

#include "common/utils.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace transport {

class Processor {
public:
    static Processor* Instance();

    inline void RegisterProcessor(uint32_t type, MessageProcessor processor) {
        assert(type < common::kLegoMaxMessageTypeCount);
        message_processor_[type] = processor;
    }

    inline void HandleMessage(MessagePtr& msg_ptr) {
        auto& message = msg_ptr->header;
        assert(message.type() < common::kLegoMaxMessageTypeCount);
        if (message_processor_[message.type()] == nullptr) {
            assert(false);
            return;
        }

        (message_processor_[message.type()])(msg_ptr);
    }

private:
    Processor();
    ~Processor();

    MessageProcessor message_processor_[common::kLegoMaxMessageTypeCount];

    DISALLOW_COPY_AND_ASSIGN(Processor);
};

}  // namespace transport

}  // namespace zjchain
