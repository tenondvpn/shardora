#pragma once

#include <unordered_set>
#include <deque>

#include "common/utils.h"

namespace zjchain {

namespace common {

template<class KeyType, class ValueType>
class  LimitHashMap {
public:
    LimitHashMap() : max_size_(64) {}
    LimitHashMap(uint32_t max_size) : max_size_(max_size) {}
    ~LimitHashMap() {}
    bool KeyExists(const KeyType& key) {
        auto iter = item_map_.find(key);
        if (iter != item_map_.end()) {
            return true;
        }

        return false;
    }

    bool Get(const KeyType& key, ValueType* val) {
        auto iter = item_map_.find(key);
        if (iter == item_map_.end()) {
            return false;
        }

        *val = iter->second;
        return true;
    }

    void Insert(const KeyType& key, const ValueType& val) {
        auto iter = item_map_.find(key);
        bool finded = (iter != item_map_.end());
        item_map_[key] = val;
        if (finded) {
            return;
        }

        item_queue_.push_back(key);
        while (item_queue_.size() > max_size_) {
            auto old_key = item_queue_.front();
            auto riter = item_map_.find(old_key);
            if (riter != item_map_.end()) {
                item_map_.erase(riter);
            }

            item_queue_.pop_front();
        }
    }

private:
    uint32_t max_size_{ 0 };
    std::deque<KeyType> item_queue_;
    std::unordered_map<KeyType, ValueType> item_map_;

    DISALLOW_COPY_AND_ASSIGN(LimitHashMap);
};

};  // namespace common

};  // namespace zjchain