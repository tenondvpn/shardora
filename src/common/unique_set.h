#pragma once

#include <queue>

#include "common/fixed_queue.h"
#include "common/hash.h"

namespace zjchain {

namespace common {

template<class T, uint32_t BucketSize, uint8_t EachBucketSize>
class UniqueSet {
public:
    explicit UniqueSet() {
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

template<uint32_t BucketSize, uint8_t EachBucketSize>
class StringUniqueSet {
public:
    explicit StringUniqueSet() {
        buckets_ = new common::FixedQueue<std::string*, EachBucketSize>[BucketSize];
    }

    ~StringUniqueSet() {
        for (uint32_t idx = 0; idx < BucketSize; ++idx) {
            while (!buckets_[idx].IsEmpty()) {
                auto* item = buckets_[idx].Front();
                buckets_[idx].Dequeue();
                delete item;
            }
        }

        delete[] buckets_;
    }

    bool add(const std::string& key) {
        uint32_t idx = Hash32(key) % BucketSize;
        if (Exists(idx, key)) {
            return false;
        }

        if (buckets_[idx].IsFull()) {
            auto* item = buckets_[idx].Front();
            buckets_[idx].Dequeue();
            delete item;
        }

        buckets_[idx].Enqueue(new std::string(key));
        return true;
    }

    bool exists(const std::string& key) {
        uint32_t idx = Hash32(key) % BucketSize;
        return Exists(idx, key);
    }

private:
    bool Exists(uint32_t idx, const std::string& key) {
        if (!buckets_[idx].IsEmpty()) {
            if (buckets_[idx].rear_ == buckets_[idx].front_) {
                for (uint8_t i = 0; i < EachBucketSize; ++i) {
                    if (*buckets_[idx].data_[i] == key) {
                        return true;
                    }
                }
            } else if (buckets_[idx].rear_ > buckets_[idx].front_) {
                for (uint8_t i = buckets_[idx].front_; i < buckets_[idx].rear_; ++i) {
                    if (*buckets_[idx].data_[i] == key) {
                        return true;
                    }
                }
            } else {
                for (uint8_t i = buckets_[idx].front_; i < EachBucketSize; ++i) {
                    if (*buckets_[idx].data_[i] == key) {
                        return true;
                    }
                }

                for (uint8_t i = 0; i < buckets_[idx].rear_; ++i) {
                    if (*buckets_[idx].data_[i] == key) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    common::FixedQueue<std::string*, EachBucketSize>* buckets_ = nullptr;
};

template<uint32_t BucketSize, uint8_t EachBucketSize>
class StringPointerUniqueSet {
public:
    explicit StringPointerUniqueSet() {
        buckets_ = new common::FixedQueue<std::string*, EachBucketSize>[BucketSize];
    }

    ~StringPointerUniqueSet() {
        for (uint32_t idx = 0; idx < BucketSize; ++idx) {
            while (!buckets_[idx].IsEmpty()) {
                auto* item = buckets_[idx].Front();
                buckets_[idx].Dequeue();
                delete item;
            }
        }

        delete[] buckets_;
    }

    bool add(const std::string* key) {
        uint32_t idx = Hash32(*key) % BucketSize;
        if (Exists(idx, key)) {
            return false;
        }

        if (buckets_[idx].IsFull()) {
            auto* item = buckets_[idx].Front();
            buckets_[idx].Dequeue();
            delete item;
        }

        buckets_[idx].Enqueue(key);
        return true;
    }

    bool exists(const std::string* key) {
        uint32_t idx = Hash32(*key) % BucketSize;
        return Exists(idx, key);
    }

private:
    bool Exists(uint32_t idx, const std::string* key) {
        if (!buckets_[idx].IsEmpty()) {
            if (buckets_[idx].rear_ == buckets_[idx].front_) {
                for (uint8_t i = 0; i < EachBucketSize; ++i) {
                    if (*buckets_[idx].data_[i] == *key) {
                        return true;
                    }
                }
            } else if (buckets_[idx].rear_ > buckets_[idx].front_) {
                for (uint8_t i = buckets_[idx].front_; i < buckets_[idx].rear_; ++i) {
                    if (*buckets_[idx].data_[i] == *key) {
                        return true;
                    }
                }
            } else {
                for (uint8_t i = buckets_[idx].front_; i < EachBucketSize; ++i) {
                    if (*buckets_[idx].data_[i] == *key) {
                        return true;
                    }
                }

                for (uint8_t i = 0; i < buckets_[idx].rear_; ++i) {
                    if (*buckets_[idx].data_[i] == *key) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    common::FixedQueue<std::string*, EachBucketSize>* buckets_ = nullptr;
};

}  // namespace common

}  // namespace zjchain
