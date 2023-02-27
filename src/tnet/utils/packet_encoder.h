#pragma once

// #include "tnet/tnet_utils.h"
#include "tnet/utils/packet.h"
#include "tnet/utils/bytes_buffer.h"

namespace zjchain {

namespace tnet {

class PacketEncoder {
public:
    virtual bool Encode(const Packet& packet, ByteBuffer* buffer) = 0;
    virtual void Free() = 0;

protected:
    PacketEncoder() {}
    virtual ~PacketEncoder() {}

};

}  // namespace tnet

}  // namespace zjchain
