#pragma once

#include <mutex>
#include "common/utils.h"
#include "transport/transport_utils.h"
#include <common/log.h>

namespace shardora {

namespace transport {

class Processor {
public:
    static Processor* Instance();

    inline void RegisterProcessor(uint32_t type, MessageProcessor processor) {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        message_processor_[type] = std::move(processor);
        SHARDORA_DEBUG("success register message type: %d", type);
    }

    inline void HandleMessage(MessagePtr& msg_ptr) {
        auto& message = msg_ptr->header;
        MessageProcessor handler;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            handler = message_processor_[message.type()];
        }
        if (!handler) {
            SHARDORA_ERROR("error msg type: %d", message.type());
            return;
        }

        ADD_DEBUG_PROCESS_TIMESTAMP();
        handler(msg_ptr);
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }

private:
    Processor();
    ~Processor();

    MessageProcessor message_processor_[common::kMaxMessageTypeCount];
    std::mutex handler_mutex_;

    DISALLOW_COPY_AND_ASSIGN(Processor);
};

}  // namespace transport

}  // namespace shardora
