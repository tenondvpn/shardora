#pragma once

#include <queue>

#include "common/hash.h"

namespace zjchain {

namespace common {

template<class KeyType, class ValueType, uint8_t BucketSize, uint8_t EachBucketSize>
class UniqueMap {
    struct Item {
        Item(const KeyType& key, const ValueType& val) : k(key), v(val) {}
        KeyType k;
        ValueType v;
    };

public:
    explicit UniqueMap() {}
    void Init(uint32_t bucket_count, uint32_t max_save) {
        buckets_ = new common::FixedQueue<T, EachBucketSize>[BucketSize];
    }

    ~UniqueMap() {
        if (buckets_ == nullptr) {
            return;
        }

        for (uint32_t idx = 0; idx < BucketSize; ++idx) {
            while (!buckets_[idx].IsEmpty()) {
                auto* item = buckets_[idx].Front();
                buckets_[idx].Dequeue();
                delete item;
            }
        }

        delete[] buckets_;
    }
    
    bool get(const KeyType& key, ValueType* value) {
        uint32_t idx = Hash32(key) % BucketSize;
        if (!buckets_[idx].IsEmpty()) {
            uint8_t i = buckets_[idx].rear_;
            for (; i <= buckets_[idx].front_ && i < EachBucketSize; ++i) {
                if (buckets_[idx].data_[i].k == key) {
                    *value = buckets_[idx].data_[i].v;
                    return true;
                }
            }

            if (i == buckets_[idx].front_) {
                return false;
            }

            if (i == EachBucketSize) {
                i = 0;
            }

            for (; i <= buckets_[idx].front_ && i < EachBucketSize; ++i) {
                if (buckets_[idx].data_[i].k == key) {
                    *value = buckets_[idx].data_[i].v;
                    return true;
                }
            }
        }

        return false;
    }

    bool add(const KeyType& key, const ValueType& value) {
        uint32_t idx = Hash32(key) % BucketSize;
        if (Exists(idx, key)) {
            return false;
        }

        if (buckets_[idx].IsFull()) {
            auto* item = buckets_[idx].Front();
            buckets_[idx].Dequeue();
            delete item;
        }

        auto item = new Item(key, value);
        buckets_[idx].Enqueue(item);
        return true;
    }

private:
    bool Exists(uint32_t idx, const KeyType& key) {
        if (!buckets_[idx].IsEmpty()) {
            uint8_t i = buckets_[idx].rear_;
            for (; i <= buckets_[idx].front_ && i < EachBucketSize; ++i) {
                if (buckets_[idx].data_[i].k == key) {
                    return true;
                }
            }

            if (i == buckets_[idx].front_) {
                return false;
            }

            if (i == EachBucketSize) {
                i = 0;
            }

            for (; i <= buckets_[idx].front_ && i < EachBucketSize; ++i) {
                if (buckets_[idx].data_[i].k == key) {
                    return true;
                }
            }
        }

        return false;
    }

    std::deque<Item*>* buckets_ = nullptr;

};

}  // namespace common

}  // namespace zjchain
