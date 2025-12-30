#pragma once

#include <list>
#include <unordered_map>

#include "common/fixed_queue.h"
#include "common/hash.h"

namespace shardora {

namespace common {

template<class T, uint32_t kMaxSize>
class UniqueSet {
public:
    explicit UniqueSet() {
    }

    ~UniqueSet() {
    }

    size_t size() const {
        return item_list_.size();
    }

    bool add(const T& key) {
        auto iter = item_map_.find(key);
        if (iter != item_map_.end()) {
            return false;
        }

        item_list_.push_back(key);
        auto end_iter = item_list_.end();
        item_map_[key] = --end_iter;
        if (item_list_.size() > kMaxSize) {
            iter = item_map_.find(item_list_.front());
            item_map_.erase(iter);
            item_list_.pop_front();
        }

        return true;
    }

    bool exists(const T& key) {
        auto iter = item_map_.find(key);
        return iter != item_map_.end();
    }

private:
    std::list<T> item_list_;
    std::unordered_map<T, typename std::list<T>::iterator> item_map_;
};

}  // namespace common

}  // namespace shardora
