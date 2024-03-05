#pragma once

#include <deque>

#include "tnet/utils/packet_decoder.h"
#include "tnet/utils/msg_packet.h"

namespace zjchain {

namespace transport {

class MultiThreadHandler;
class MsgDecoder: public tnet::PacketDecoder {
public:
    MsgDecoder(MultiThreadHandler* multi_thread_handler);
    virtual ~MsgDecoder();
    virtual bool Decode(const std::string& from_ip, const char* buf, size_t len);
    virtual tnet::Packet* GetPacket();
    virtual void Free();

private:
    bool GetPacketLen(const std::string& from_ip, const char* buf, size_t len, size_t& pos);

    std::deque<tnet::MsgPacket*> packet_list_;
    std::string tmp_str_;
    uint32_t packet_len_{ 0 };
    uint32_t type_{ 0 };
    MultiThreadHandler* multi_thread_handler_ = nullptr;
};

}  // namespace transport

}  // namespace zjchain
