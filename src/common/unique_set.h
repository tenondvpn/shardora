#pragma once

#include <queue>

#include "common/fixed_queue.h"
#include "common/hash.h"

namespace zjchain {

namespace common {

template<class T, uint8_t BucketSize, uint8_t EachBucketSize>
class UniqueSet {
public:
    explicit UniqueSet() {}

    void Init(uint32_t bucket_count) {
        buckets_ = new common::FixedQueue<T, EachBucketSize>[BucketSize];
    }

    ~UniqueSet() {
        delete[] buckets_;
    }

    bool add(const T& key) {
        uint32_t idx = Hash32(key) % BucketSize;
        if (buckets_[idx].Exists(key)) {
            return false;
        }

        if (buckets_[idx].IsFull()) {
            buckets_[idx].Dequeue();
        }

        buckets_[idx].Enqueue(key);
        return true;
    }

    bool exists(const T& key) {
        uint32_t idx = Hash32(key) % BucketSize;
        return buckets_[idx].Exists(key);
    }

private:
    common::FixedQueue<T, EachBucketSize>* buckets_ = nullptr;
};

}  // namespace common

}  // namespace zjchain
