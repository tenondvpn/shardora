#pragma once

#include <stdint.h>

namespace zjchain {

namespace tnet {


enum TcpPacketType {
    kProtobuff = 0,
    kRaw = 1,
};

enum TcpEncodeType {
    kEncodeWithHeader = 0,
    kEncodeRaw = 1,
};

enum RelayType {
    kHandshake = 0,
    kStream = 1,
};

class Packet {
public:
    virtual void Free() = 0;
    virtual bool IsCmdPacket() const = 0;
    virtual uint32_t PacketType() const = 0;
    virtual uint32_t EncodeType() const = 0;

protected:
    Packet() {}
    virtual ~Packet() {}

};


}  // namespace tnet

}  // namespace zjchain
