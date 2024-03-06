#pragma once

#include <unordered_map>
#include <list>

#include "common/utils.h"

namespace zjchain {

namespace common {

template<class Type>
class LRUSet {
public:
    LRUSet(uint32_t max_size) : max_size_(max_size) {}
    ~LRUSet() {}
    bool DataExists(const Type& data) {
        return item_map_.count(data);
    }

    bool Push(const Type& data) {
        bool ret = true;
        if (item_map_.count(data)) {
            item_list_.erase(item_map_[data]);
            item_map_.erase(data);
            ret = false;
        }

        item_list_.push_front(data);
        item_map_[data] = item_list_.begin();

        if (item_list_.size() > max_size_) {
            Type last = item_list_.back();
            item_list_.pop_back();
            item_map_.erase(last);
        }

        return ret;
    }

    void Reset() {
        item_list_.clear();
        item_map_.clear();
    }

private:
    uint32_t max_size_{ 0 };
    std::list<Type> item_list_;
    std::unordered_map<Type, typename std::list<Type>::iterator> item_map_;

    DISALLOW_COPY_AND_ASSIGN(LRUSet<Type>);
};

};  // namespace common

};  // namespace zjchain
