#pragma once

#include <queue>

#include "common/fixed_queue.h"
#include "common/hash.h"

namespace zjchain {

namespace common {

template<class KeyType, class ValueType, uint32_t BucketSize, uint8_t EachBucketSize>
class UniqueMap {
    struct Item {
        Item(const KeyType& key, const ValueType& val) : k(key), v(val) {}
        KeyType k;
        ValueType v;
    };

public:
    explicit UniqueMap() {
        buckets_ = new common::FixedQueue<Item*, EachBucketSize>[BucketSize];
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
            if (buckets_[idx].rear_ == buckets_[idx].front_) {
                for (uint8_t i = 0; i < EachBucketSize; ++i) {
                    if (buckets_[idx].data_[i]->k == key) {
                        *value = buckets_[idx].data_[i]->v;
                        return true;
                    }
                }
            } else if (buckets_[idx].rear_ > buckets_[idx].front_) {
                for (uint8_t i = buckets_[idx].front_; i < buckets_[idx].rear_; ++i) {
                    if (buckets_[idx].data_[i]->k == key) {
                        *value = buckets_[idx].data_[i]->v;
                        return true;
                    }
                }
            } else {
                for (uint8_t i = buckets_[idx].front_; i < EachBucketSize; ++i) {
                    if (buckets_[idx].data_[i]->k == key) {
                        *value = buckets_[idx].data_[i]->v;
                        return true;
                    }
                }

                for (uint8_t i = 0; i < buckets_[idx].rear_; ++i) {
                    if (buckets_[idx].data_[i]->k == key) {
                        *value = buckets_[idx].data_[i]->v;
                        return true;
                    }
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

    void update(const KeyType& key, const ValueType& value) {
        if (!exists(key)) {
            add(key, value);
            return;
        }

        uint32_t idx = Hash32(key) % BucketSize;
        if (buckets_[idx].rear_ == buckets_[idx].front_) {
            for (uint8_t i = 0; i < EachBucketSize; ++i) {
                if (buckets_[idx].data_[i]->k == key) {
                    buckets_[idx].data_[i]->v = value;
                    return;
                }
            }
        } else if (buckets_[idx].rear_ > buckets_[idx].front_) {
            for (uint8_t i = buckets_[idx].front_; i < buckets_[idx].rear_; ++i) {
                if (buckets_[idx].data_[i]->k == key) {
                    buckets_[idx].data_[i]->v = value;
                    return;
                }
            }
        } else {
            for (uint8_t i = buckets_[idx].front_; i < EachBucketSize; ++i) {
                if (buckets_[idx].data_[i]->k == key) {
                    buckets_[idx].data_[i]->v = value;
                    return;
                }
            }

            for (uint8_t i = 0; i < buckets_[idx].rear_; ++i) {
                if (buckets_[idx].data_[i]->k == key) {
                    buckets_[idx].data_[i]->v = value;
                    return;
                }
            }
        }
    }

    bool exists(const KeyType& key) {
        uint32_t idx = Hash32(key) % BucketSize;
        return Exists(idx, key);
    }

private:
    bool Exists(uint32_t idx, const KeyType& key) {
        if (!buckets_[idx].IsEmpty()) {
            if (buckets_[idx].rear_ == buckets_[idx].front_) {
                for (uint8_t i = 0; i < EachBucketSize; ++i) {
                    if (buckets_[idx].data_[i]->k == key) {
                        return true;
                    }
                }
            } else if (buckets_[idx].rear_ > buckets_[idx].front_) {
                for (uint8_t i = buckets_[idx].front_; i < buckets_[idx].rear_; ++i) {
                    if (buckets_[idx].data_[i]->k == key) {
                        return true;
                    }
                }
            } else {
                for (uint8_t i = buckets_[idx].front_; i < EachBucketSize; ++i) {
                    if (buckets_[idx].data_[i]->k == key) {
                        return true;
                    }
                }

                for (uint8_t i = 0; i < buckets_[idx].rear_; ++i) {
                    if (buckets_[idx].data_[i]->k == key) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    common::FixedQueue<Item*, EachBucketSize>* buckets_ = nullptr;

};

}  // namespace common

}  // namespace zjchain
