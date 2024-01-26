#pragma once

#include <cstdint>
#include <vector>
#include <cassert>
#include <string>

namespace zjchain {

namespace common {

class BloomFilter {
public:
    BloomFilter() {}
    BloomFilter(uint32_t bit_count, uint32_t hash_count);
    BloomFilter(const std::vector<uint64_t>& data, uint32_t hash_count);
    ~BloomFilter();
    void Add(uint64_t hash);
    bool Contain(uint64_t hash) const;
    uint32_t DiffCount(const BloomFilter& other);
    BloomFilter& operator=(const BloomFilter& src);
    bool operator==(const BloomFilter& r) const;
    bool operator!=(const BloomFilter& r) const;

    const std::vector<uint64_t>& data() const {
        assert(data_.size() <= 256);
        return data_;
    }

    uint32_t hash_count() {
        return hash_count_;
    }

    std::string Serialize() const;
    void Deserialize(const uint64_t* data, uint32_t count, uint32_t hash_count);

private:
    std::vector<uint64_t> data_;
    uint32_t hash_count_{ 0 };
};

}  // namespace common

}  // namespace zjchain
