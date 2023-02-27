#pragma once

#include "tnet/utils/bytes_buffer.h"
#include "tnet/utils/packet_encoder.h"
#include "tnet/utils/msg_packet.h"

namespace zjchain {

namespace transport {

class MsgEncoder : public tnet::PacketEncoder {
public:
    MsgEncoder() {}

    virtual ~MsgEncoder() {}

    virtual bool Encode(const tnet::Packet& packet, tnet::ByteBuffer* buffer) {
        tnet::MsgPacket* msg_packet = const_cast<tnet::MsgPacket*>(
                dynamic_cast<const tnet::MsgPacket*>(&packet));
        if (msg_packet == NULL) {
            return false;
        }

        char* data = nullptr;
        uint32_t len = 0;
        msg_packet->GetMessageEx(&data, &len);
        if (data == nullptr) {
            return false;
        }

        if (packet.EncodeType() == tnet::kEncodeWithHeader) {
            tnet::PacketHeader header(len, msg_packet->PacketType());
            buffer->Append((char*)&header, sizeof(header));
        }

        buffer->Append(data, len);
        return true;
    }

    virtual void Free() {
        delete this;
    }
};

}  // namespace transport

}  // namespace zjchain
