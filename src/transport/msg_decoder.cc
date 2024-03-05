#include "transport/msg_decoder.h"

namespace zjchain {

namespace transport {

MsgDecoder::MsgDecoder() {}

MsgDecoder::~MsgDecoder() {
    for (auto iter = packet_list_.begin(); iter != packet_list_.end(); ++iter) {
        delete *iter;
    }

    packet_list_.clear();
}

bool MsgDecoder::GetPacketLen(const char* buf, size_t len, size_t& pos) {
    if (tmp_str_.empty()) {
        if ((len - pos) < sizeof(tnet::PacketHeader)) {
            tmp_str_.assign(buf + pos, len - pos);
            pos = len;
            return true;
        } else {
            tnet::PacketHeader* header = (tnet::PacketHeader*)(buf + pos);
            packet_len_ = header->length;
            type_ = header->type;
            pos += sizeof(tnet::PacketHeader);
            return true;
        }
    } else {
        size_t header_left = sizeof(tnet::PacketHeader) - tmp_str_.size();
        if (len < header_left) {
            tmp_str_.append(buf + pos, len - pos);
            pos += len;
            return true;
        } else {
            tmp_str_.append(buf + pos, header_left);
            tnet::PacketHeader* header = (tnet::PacketHeader*)tmp_str_.c_str();
            packet_len_ = header->length;
            type_ = header->type;
            pos += header_left;
            tmp_str_.clear();
            return true;
        }
    }

    return false;
}

bool MsgDecoder::Decode(const char* buf, size_t len) {
    if (len == 0) {
        return true;
    }

    size_t pos = 0;
    if (packet_len_ == 0) {
        if (!GetPacketLen(buf, len, pos)) {
            return false;
        }
    }

    if (packet_len_ == 0) {
        return true;
    }

    while (pos < len) {
        if ((tmp_str_.size() + len - pos) >= packet_len_) {
            size_t left_len = packet_len_ - tmp_str_.size();
            std::string* msg = new std::string();
            if (tmp_str_.empty()) {
                msg->assign(buf + pos, left_len);
            } else {
                msg->append(tmp_str_);
                msg->append(buf + pos, left_len);
                tmp_str_.clear();
            }
            pos += left_len;
            tnet::MsgPacket* packet = new tnet::MsgPacket(type_, tnet::kEncodeWithHeader, true);
            packet->SetMessage(msg);
            packet_list_.push_back(packet);
            packet_len_ = 0;
            if (pos < len) {
                if (!GetPacketLen(buf, len, pos)) {
                    return false;
                }
            }
        } else {
            tmp_str_.append(buf + pos, len - pos);
            break;
        }
    }

    return true;
}

tnet::Packet* MsgDecoder::GetPacket() {
    if (packet_list_.empty()) {
        return NULL;
    }
    tnet::Packet* packet = packet_list_.front();
    packet_list_.pop_front();
    return packet;
}

void MsgDecoder::Free() {
    delete this;
}

}  // namespace transport

}  // namespace zjchain
