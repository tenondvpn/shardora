#pragma once

#include <string.h>

#include <vector>
#include <memory>

namespace zjchain {

namespace tnet {

class ByteBuffer {
public:
    ByteBuffer() : offset_(0) {}

    ~ByteBuffer() {}

    const uint8_t* data() const {
        if (data_.empty()) {
            return NULL;
        }

        return &data_[offset_];
    }

    size_t length() const {
        return data_.size() - offset_;
    }

    size_t size() const {
        return data_.size();
    }

    size_t AddOffset(size_t len) {
        offset_ += len;
        return offset_;
    }

    void Append(const void* data, size_t len) {
        if (data != NULL && len != 0) {
            size_t oldLen = data_.size();
            data_.resize(oldLen + len);
            memcpy(&data_[oldLen], data, len);
        }
    }

    void SwapData(std::vector<uint8_t>& data) {
        data_.swap(data);
    }

private:
    size_t offset_;
    std::vector<uint8_t> data_;
};

typedef std::shared_ptr<ByteBuffer> ByteBufferPtr;

}  // namespace tnet

}  // namespace zjchain
