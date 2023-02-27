#pragma once

#include <cstdint>
#include <vector>

namespace zjchain {

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

private:
    std::vector<uint64_t> data_;
    uint32_t valid_count_{ 0 };
};

}  // namespace common

}  // namespace zjchain
