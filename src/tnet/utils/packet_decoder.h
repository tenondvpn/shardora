#pragma once

// #include "tnet/tnet_utils.h"
#include "tnet/utils/packet.h"
#include "tnet/utils/bytes_buffer.h"

namespace zjchain {

namespace tnet {

class PacketDecoder {

public:
    virtual bool Decode(const char* buf, size_t len) = 0;
    virtual Packet* GetPacket() = 0;
    virtual void Free() = 0;

protected:
    PacketDecoder() {}
    virtual ~PacketDecoder() {}

};

}  // namespace tnet

}  // namespace zjchain
