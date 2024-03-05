#pragma once

#include "tnet/utils/packet_factory.h"
#include "transport/msg_encoder.h"
#include "transport/msg_decoder.h"

namespace zjchain {

namespace transport {

class MultiThreadHandler;
class EncoderFactory : public tnet::PacketFactory {
public:
    virtual tnet::PacketEncoder* CreateEncoder() {
        return new MsgEncoder();
    }

    virtual tnet::PacketDecoder* CreateDecoder() {
        return new MsgDecoder(multi_thread_handler_);
    }

    EncoderFactory(MultiThreadHandler* multi_thread_handler) {
        multi_thread_handler_ = multi_thread_handler;
    }

    virtual ~EncoderFactory() {}
    MultiThreadHandler* multi_thread_handler_ = nullptr;
};

}  // namespace transport

}  // namespace zjchain
