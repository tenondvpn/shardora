#pragma once

// #include "tnet/tnet_utils.h"
#include "tnet/utils/packet.h"
#include "tnet/utils/bytes_buffer.h"
#include "tnet/utils/packet_decoder.h"
#include "tnet/utils/packet_encoder.h"

namespace zjchain {

namespace tnet {

class PacketFactory {
public:
    virtual PacketEncoder* CreateEncoder() = 0;
    virtual PacketDecoder* CreateDecoder() = 0;

protected:
    PacketFactory() {}
    virtual ~PacketFactory() {}

};

}  // namespace tnet

}  // namespace zjchain
