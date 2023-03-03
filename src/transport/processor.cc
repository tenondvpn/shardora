#include "transport/processor.h"

namespace zjchain {

namespace transport {

Processor* Processor::Instance() {
    static Processor ins;
    return &ins;
}

void Processor::RegisterProcessor(uint32_t type, MessageProcessor processor) {
    assert(type < common::kLegoMaxMessageTypeCount);
    message_processor_[type].push_back(processor);
}

void Processor::UnRegisterProcessor(uint32_t type) {
    assert(type < common::kLegoMaxMessageTypeCount);
    message_processor_[type].clear();
}

void Processor::HandleMessage(MessagePtr& msg_ptr) {
    auto& message = msg_ptr->header;
    assert(message.type() < common::kLegoMaxMessageTypeCount);
    if (message_processor_[message.type()].empty()) {
        return;
    }

    for (auto iter = message_processor_[message.type()].begin();
            iter != message_processor_[message.type()].end(); ++iter) {
        (*iter)(msg_ptr);
    }
}

Processor::Processor() {}

Processor::~Processor() {}

}  // namespace transport

}  // namespace zjchain
