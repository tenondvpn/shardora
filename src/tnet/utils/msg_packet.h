#pragma once

#include <iostream>
#include "tnet/utils/packet.h"

namespace zjchain {

namespace tnet {

struct PacketHeader {
    explicit PacketHeader(uint32_t len, uint32_t in_type)
            : length(len), type(in_type) {}
    uint32_t length : 24;
    uint32_t type : 8;
};

class MsgPacket : public tnet::Packet {
public:
    MsgPacket(uint32_t type, uint32_t encode_type, bool free_data)
            : type_(type), encode_type_(encode_type), free_data_(free_data) {}
    virtual ~MsgPacket() {}

    virtual void Free() {
        if (free_data_) {
            if (message_data_ != nullptr) {
                delete[] message_data_;
            }

            if (message_ != nullptr) {
                delete message_;
            }
        }

        delete this;
    }

    virtual bool IsCmdPacket() const {
        return false;
    }

    virtual uint32_t PacketType() const {
        return type_;
    }

    virtual uint32_t EncodeType() const {
        return encode_type_;
    }

//     std::string* GetMessageEx() {
//         return message_;
//     }

    void GetMessageEx(char** data, uint32_t* len) {
        if (message_ != nullptr) {
            *data = (char*)message_->data();
            *len = message_->size();
            return;
        }

        if (message_data_ != nullptr) {
            *data = message_data_;
            *len = message_len_;
            return;
        }
    }

    void SetMessage(std::string* message) {
        message_ = message;
    }

    void SetMessage(char* data, uint32_t len) {
        message_data_ = data;
        message_len_ = len;
    }

private:
    std::string* message_{ nullptr };
    uint32_t type_{ 0 };
    uint32_t encode_type_{ 0 };
    char* message_data_{ nullptr };
    uint32_t message_len_{ 0 };
    bool free_data_{ false };
};

}  // namespace tnet

}  // namespace zjchain
