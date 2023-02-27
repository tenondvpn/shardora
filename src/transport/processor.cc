#include "transport/processor.h"

namespace zjchain {

namespace transport {

Processor* Processor::Instance() {
    static Processor ins;
    return &ins;
}

void Processor::RegisterProcessor(uint32_t type, MessageProcessor processor) {
    assert(type < common::kLegoMaxMessageTypeCount);
#ifndef ZJC_UNITTEST
    assert(message_processor_[type] == nullptr);
#endif
    message_processor_[type] = processor;
}

void Processor::UnRegisterProcessor(uint32_t type) {
    assert(type < common::kLegoMaxMessageTypeCount);
    message_processor_[type] = nullptr;
}

void Processor::HandleMessage(MessagePtr& msg_ptr) {
    auto& message = msg_ptr->header;
    assert(message.type() < common::kLegoMaxMessageTypeCount);
    if (message_processor_[message.type()] == nullptr) {
        return;
    }

    message_processor_[message.type()](msg_ptr);
}

Processor::Processor() {
    for (uint32_t i = 0; i < common::kLegoMaxMessageTypeCount; ++i) {
        message_processor_[i] = nullptr;
    }
}

Processor::~Processor() {}

}  // namespace transport

}  // namespace zjchain
