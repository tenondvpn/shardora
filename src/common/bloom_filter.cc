#include <cassert>
#include <iostream>

#include "common/bloom_filter.h"
#include "common/u16_bit_count.h"

namespace zjchain {

namespace common {

BloomFilter::BloomFilter(uint32_t bit_count, uint32_t hash_count) : hash_count_(hash_count) {
    assert((bit_count % 64) == 0);
    uint32_t data_cnt = bit_count / 64;
    for (uint32_t i = 0; i < data_cnt; ++i) {
        data_.push_back(0ull);
    }
    assert(!data_.empty());
}

void BloomFilter::Deserialize(const uint64_t* data, uint32_t count, uint32_t hash_count) {
    data_.clear();
    for (uint32_t i = 0; i < count; ++i) {
        data_.push_back(data[i]);
    }

    hash_count_ = hash_count;
}

std::string BloomFilter::Serialize() const {
    assert(!data_.empty());
    uint64_t* data = new uint64_t[data_.size()];
    for (uint32_t i = 0; i < data_.size(); ++i) {
        data[i] = data_[i];
    }

    std::string res((char*)data, data_.size() * sizeof(data[0]));
    delete[] data;
    return res;
}

BloomFilter::BloomFilter(const std::vector<uint64_t>& data, uint32_t hash_count)
        : data_(data), hash_count_(hash_count) {}

BloomFilter::~BloomFilter() {}

void BloomFilter::Add(uint64_t hash) {
    uint32_t hash_high = static_cast<uint32_t>((hash >> 32) & 0x00000000FFFFFFFFull);
    uint32_t hash_low = static_cast<uint32_t>(hash & 0x00000000FFFFFFFFull);
    for (uint32_t i = 0; i < hash_count_; ++i) {
        uint32_t index = (hash_high + i * hash_low);
        uint32_t vec_index = (index % (64 * data_.size())) / 64;
        uint32_t bit_index = (index % (64 * data_.size())) % 64;
        data_[vec_index] |= (uint64_t)((uint64_t)(1ull) << bit_index);
    }
}

bool BloomFilter::Contain(uint64_t hash) const{
    uint32_t hash_high = static_cast<uint32_t>((hash >> 32) & 0x00000000FFFFFFFFull);
    uint32_t hash_low = static_cast<uint32_t>(hash & 0x00000000FFFFFFFFull);
    for (uint32_t i = 0; i < hash_count_; ++i) {
        uint32_t index = (hash_high + i * hash_low);
        uint32_t vec_index = (index % (64 * data_.size())) / 64;
        uint32_t bit_index = (index % (64 * data_.size())) % 64;
        if ((data_[vec_index] & ((uint64_t)((uint64_t)(1ull) << bit_index))) == 0ull) {
            return false;
        }
    }

    return true;
}

uint32_t BloomFilter::DiffCount(const BloomFilter& other) {
    if (data_.size() != other.data_.size()) {
        ZJC_ERROR("data_.size()[%u] != other.data_.size()[%u]",
            data_.size(), other.data_.size());
        return (std::numeric_limits<uint32_t>::max)();
    }

    uint32_t diff_count = 0;
    for (uint32_t i = 0; i < data_.size(); ++i) {
        uint16_t* u16_data_l = (uint16_t*)(&data_[i]);
        uint16_t* u16_data_r = (uint16_t*)(&other.data_[i]);
        for (uint32_t i = 0; i < 4; ++i) {
            diff_count += common::U16BitCount::Instance()->DiffCount(
                u16_data_l[i] ^ u16_data_r[i]);
        }
    }

    return diff_count;
}

BloomFilter& BloomFilter::operator=(const BloomFilter& src) {
    if (this == &src) {
        return *this;
    }

    data_ = src.data_;
    hash_count_ = src.hash_count_;
    return *this;
}

bool BloomFilter::operator==(const BloomFilter& r) const {
    if (this == &r) {
        return true;
    }

    return (data_ == r.data_ && hash_count_ == r.hash_count_);
}

bool BloomFilter::operator!=(const BloomFilter& r) const {
    if (this == &r) {
        return true;
    }

    return !(data_ == r.data_ && hash_count_ == r.hash_count_);
}

}  // namespace common

}  // namespace zjchain