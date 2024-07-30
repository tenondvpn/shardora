#pragma once

#include <cstdint>
#include <vector>

namespace shardora {

namespace common {

class Bitmap {
public:
    Bitmap() {}
    Bitmap(uint32_t bit_count);
    Bitmap(const std::vector<uint64_t>& data);
    Bitmap(const Bitmap& src);
    ~Bitmap();
    void Set(uint32_t bit_index);
    void UnSet(uint32_t bit_index);
    bool Valid(uint32_t bit_index) const;
    Bitmap& operator=(const Bitmap& src);
    bool operator==(const Bitmap& r) const;

    const std::vector<uint64_t>& data() const {
        return data_;
    }

    uint32_t valid_count() const {
        return valid_count_;
    }

    void clear() {
        for (uint32_t i = 0; i < data_.size(); ++i) {
            data_[i] = 0;
        }

        valid_count_ = 0;
    }

    void inversion(uint32_t max_idx) {
        uint32_t u64_count = max_idx / 64;
        for (uint32_t i = 0; i < u64_count; ++i) {
            data_[i] = ~data_[i];
        }

        auto valid_count = valid_count_;
        for (uint32_t i = u64_count * 64; i < max_idx; ++i) {
            if (Valid(i)) {
                UnSet(i);
            } else {
                Set(i);
            }
        }

        valid_count_ = max_idx - valid_count;
    }

    Bitmap& operator<<(uint32_t shift) {
        return ShiftLeft(shift);
    }

    Bitmap& ShiftLeft(uint32_t shift) {
        if (shift == 0) return *this;

        uint32_t u64_shift = shift / 64;
        uint32_t bit_shift = shift % 64;

        if (bit_shift == 0) {
            // 完全的 64 位移位
            for (int i = data_.size() - 1; i >= u64_shift; --i) {
                data_[i] = data_[i - u64_shift];
            }
        } else {
            // 部分的 64 位移位
            for (int i = data_.size() - 1; i > u64_shift; --i) {
                data_[i] = (data_[i - u64_shift] << bit_shift) | (data_[i - u64_shift - 1] >> (64 - bit_shift));
            }
            data_[u64_shift] = data_[0] << bit_shift;
        }

        // 填充低位
        for (uint32_t i = 0; i < u64_shift; ++i) {
            data_[i] = 0;
        }
        if (bit_shift != 0) {
            data_[u64_shift - 1] = 0;
        }

        return *this;
    }    

private:
    std::vector<uint64_t> data_;
    uint32_t valid_count_{ 0 };
};

}  // namespace common

}  // namespace shardora
