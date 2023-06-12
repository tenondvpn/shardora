#include "transport/processor.h"

#include "common/time_utils.h"

namespace zjchain {

namespace transport {

Processor* Processor::Instance() {
    static Processor ins;
    return &ins;
}

void Processor::RegisterProcessor(uint32_t type, MessageProcessor processor) {
    assert(type < common::kLegoMaxMessageTypeCount);
    message_processor_[type] = processor;
}

void Processor::HandleMessage(MessagePtr& msg_ptr) {
    auto& message = msg_ptr->header;
    assert(message.type() < common::kLegoMaxMessageTypeCount);
    if (message_processor_[message.type()] == nullptr) {
        assert(false);
        return;
    }

    (message_processor_[message.type()])(msg_ptr);
}

Processor::Processor() {}

Processor::~Processor() {}

}  // namespace transport

}  // namespace zjchain
