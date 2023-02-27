#pragma once

#include <queue>

#include "common/hash.h"

namespace zjchain {

namespace common {

template<class T>
class UniqueSet {
public:
    explicit UniqueSet() {}

    void Init(uint32_t bucket_count, uint32_t max_save) {
        bucket_count_ = bucket_count;
        max_save_ = max_save;
        buckets_ = new std::deque<T>[bucket_count_];
    }

    ~UniqueSet() {
        delete[] buckets_;
    }

    bool add(const T& key) {
        uint32_t idx = Hash32(key) % bucket_count_;
        if (!buckets_[idx].empty()) {
            for (auto iter = buckets_[idx].begin(); iter != buckets_[idx].end(); ++iter) {
                if ((*iter) == key) {
                    return false;
                }
            }
        }

        if (buckets_[idx].size() > max_save_) {
            buckets_[idx].pop_front();
        }

        buckets_[idx].push_back(key);
        return true;
    }

    bool exists(const T& key) {
        uint32_t idx = Hash32(key) % bucket_count_;
        if (buckets_[idx].empty()) {
            return false;
        }

        for (auto iter = buckets_[idx].begin(); iter != buckets_[idx].end(); ++iter) {
            if ((*iter) == key) {
                return true;
            }
        }

        return false;
    }

private:
    std::deque<T>* buckets_ = nullptr;
    uint32_t bucket_count_{ 1024 * 1024 };
    uint32_t max_save_ = 0;
    bool is_integer_ = false;
};

}  // namespace common

}  // namespace zjchain
