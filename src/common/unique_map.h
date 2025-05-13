#pragma once

#include <list>
#include <unordered_map>

#include "common/fixed_queue.h"
#include "common/hash.h"

namespace shardora {

namespace common {

template<class KeyType, class ValueType, uint32_t kMaxSize>
class UniqueMap {
public:
    explicit UniqueMap() {
    }

    ~UniqueMap() {
    }

    size_t size() {
        return item_list_.size();
    }

    bool add(const KeyType& key, const ValueType& value) {
        auto iter = item_map_.find(key);
        if (iter != item_map_.end()) {
            item_list_.erase(iter->second);
        }

        item_list_.push_back(key);
        auto end_iter = item_list_.end();
        item_map_[key] = --end_iter;
        kv_map_[key] = value;
        if (item_list_.size() > kMaxSize) {
            iter = item_map_.find(item_list_.front());
            item_map_.erase(iter);
            auto kv_iter = kv_map_.find(item_list_.front());
            kv_map_.erase(kv_iter);
            item_list_.pop_front();
        }
        
        return true;
    }

    bool exists(const KeyType& key) {
        auto iter = item_map_.find(key);
        return iter != item_map_.end();
    }

    bool get(const KeyType& key, ValueType* value) {
        auto iter = kv_map_.find(key);
        if (iter != kv_map_.end()) {
            *value = iter->second;
            return true;
        }

        return false;
    }

    std::unordered_map<KeyType, ValueType>::iterater begin() {
        return kv_map_.begin();
    }

    std::unordered_map<KeyType, ValueType>::iterater end() {
        return kv_map_.end();
    }

private:
    std::list<KeyType> item_list_;
    std::unordered_map<KeyType, typename std::list<KeyType>::iterator> item_map_;
    std::unordered_map<KeyType, ValueType> kv_map_;
};

}  // namespace common

}  // namespace shardora
