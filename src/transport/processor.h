#pragma once

#include "common/utils.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace transport {

class Processor {
public:
    static Processor* Instance();
    void RegisterProcessor(uint32_t type, MessageProcessor processor);
    void UnRegisterProcessor(uint32_t type);
    void HandleMessage(MessagePtr& message);

private:
    Processor();
    ~Processor();

    std::vector<MessageProcessor> message_processor_[common::kLegoMaxMessageTypeCount];

    DISALLOW_COPY_AND_ASSIGN(Processor);
};

}  // namespace transport

}  // namespace zjchain
