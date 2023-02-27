#pragma once

#include <queue>

#include "common/hash.h"

namespace zjchain {

namespace common {

template<class KeyType, class ValueType>
class UniqueMap {
    struct Item {
        Item(const KeyType& key, const ValueType& val) : k(key), v(val) {}
        KeyType k;
        ValueType v;
    };

public:
    explicit UniqueMap() {}
    void Init(uint32_t bucket_count, uint32_t max_save) {
        bucket_count_ = bucket_count;
        max_save_ = max_save;
        buckets_ = new std::deque<Item*>[bucket_count_];
    }

    ~UniqueMap() {
        if (buckets_ == nullptr) {
            return;
        }

        for (uint32_t idx = 0; idx < bucket_count_; ++idx) {
            while (!buckets_[idx].empty()) {
                auto* item = buckets_[idx].front();
                buckets_[idx].pop_front();
                delete item;
            }
        }

        delete[] buckets_;
    }
    
    bool get(const KeyType& key, ValueType* value) {
        uint32_t idx = Hash32(key) % bucket_count_;
        if (!buckets_[idx].empty()) {
            for (auto iter = buckets_[idx].begin(); iter != buckets_[idx].end(); ++iter) {
                if ((*iter)->k == key) {
                    *value = (*iter)->v;
                    return true;
                }
            }
        }

        return false;
    }

    bool add(const KeyType& key, const ValueType& value) {
        uint32_t idx = Hash32(key) % bucket_count_;
        if (!buckets_[idx].empty()) {
            for (auto iter = buckets_[idx].begin(); iter != buckets_[idx].end(); ++iter) {
                if ((*iter)->k == key) {
                    return false;
                }
            }
        }

        if (buckets_[idx].size() > max_save_) {
            auto* item = buckets_[idx].front();
            buckets_[idx].pop_front();
            delete item;
        }

        auto item = new Item(key, value);
        buckets_[idx].push_back(item);
        return true;
    }

private:
    std::deque<Item*>* buckets_ = nullptr;
    uint32_t bucket_count_{ 1024 * 1024 };
    uint32_t max_save_ = 0;
    bool is_integer_ = false;

};

}  // namespace common

}  // namespace zjchain
