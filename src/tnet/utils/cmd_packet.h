#pragma once

#include <cassert>

#include "tnet/utils/packet.h"

namespace zjchain {

namespace tnet {

class CmdPacket : public Packet {
public:
    enum Type: int {
        CT_NONE,
        CT_READ_ERROR,
        CT_WRITE_ERROR,
        CT_CONNECT_ERROR,
        CT_INVALID_PACKET,
        CT_TLS_READ_ERROR,
        CT_TLS_WRITE_ERROR,
        CT_TLS_HANDSHAKE_ERROR,
        CT_CONNECTION_CLOSED,
        CT_CONNECT_TIMEOUT,
        CT_PACKET_TIMEOUT,
        CT_HTTP_KEEPALIVE_TIMEOUT,
        CT_HTTP_IDLE_TIMEOUT,
        CT_WS_PROTOCOL_ERROR,
        CT_WS_NON_UTF8,
        CT_WS_UNEXPECTED_ERROR,
        CT_TCP_NEW_CONNECTION,
    };

    explicit CmdPacket(Type type) : type_(type) {}
    virtual ~CmdPacket() {}
    int GetType() const { return type_; }
    void SetType(Type type) { type_ = type; }
    virtual void Free() {}
    virtual bool IsCmdPacket() const { return true; }
    virtual uint32_t PacketType() const {
        return 0;
    }

    virtual uint32_t EncodeType() const {
        return 0;
    }

private:
    Type type_;
};

class CmdPacketFactory {
public:
    static CmdPacket& Create(int type);
};

}  // namespace tnet

}  // namespace zjchain
