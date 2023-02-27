#include <cassert>

#include "common/bitmap.h"

namespace zjchain {

namespace common {

Bitmap::Bitmap(uint32_t bit_count) {
    assert((bit_count % 64) == 0);
    uint32_t data_cnt = bit_count / 64;
    for (uint32_t i = 0; i < data_cnt; ++i) {
        data_.push_back(0ull);
    }
    assert(!data_.empty());
}

Bitmap::Bitmap(const std::vector<uint64_t>& data) : data_(data) {
    for (uint32_t i = 0; i < data_.size() * 64; ++i) {
        if (Valid(i)) {
            ++valid_count_;
        }
    }
}

Bitmap::~Bitmap() {}

Bitmap::Bitmap(const Bitmap& src) {
    data_ = src.data_;
    valid_count_ = src.valid_count_;
}

void Bitmap::Set(uint32_t index) {
    if (Valid(index)) {
        return;
    }

    assert(index < (data_.size() * 64));
    uint32_t vec_index = (index % (64 * data_.size())) / 64;
    uint32_t bit_index = (index % (64 * data_.size())) % 64;
    data_[vec_index] |= (uint64_t)((uint64_t)(1) << bit_index);
    ++valid_count_;
}

void Bitmap::UnSet(uint32_t index) {
    if (!Valid(index)) {
        return;
    }

    assert(index < (data_.size() * 64));
    uint32_t vec_index = (index % (64 * data_.size())) / 64;
    uint32_t bit_index = (index % (64 * data_.size())) % 64;
    data_[vec_index] &= ~((uint64_t)((uint64_t)(1) << bit_index));
    --valid_count_;
}

bool Bitmap::Valid(uint32_t index) const {
    assert(index < (data_.size() * 64));
    uint32_t vec_index = (index % (64 * data_.size())) / 64;
    uint32_t bit_index = (index % (64 * data_.size())) % 64;
    if ((data_[vec_index] & ((uint64_t)((uint64_t)(1) << bit_index))) == 0ull) {
        return false;
    }

    return true;
}

Bitmap& Bitmap::operator=(const Bitmap& src) {
    if (this == &src) {
        return *this;
    }

    data_ = src.data_;
    valid_count_ = src.valid_count_;
    return *this;
}

bool Bitmap::operator==(const Bitmap& r) const {
    if (this == &r) {
        return true;
    }

    return (data_ == r.data_ && valid_count_ == r.valid_count_);
}

}  // namespace common

}  // namespace zjchain
