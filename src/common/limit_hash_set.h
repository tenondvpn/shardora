#pragma once

#include <unordered_set>
#include <deque>

#include "common/utils.h"

namespace zjchain {

namespace common {

template<class Type>
class LimitHashSet {
public:
    LimitHashSet(uint32_t max_size) : max_size_(max_size) {}
    ~LimitHashSet() {}
    bool DataExists(const Type& data) {
        auto iter = item_set_.find(data);
        if (iter != item_set_.end()) {
            return true;
        }

        return false;
    }

    bool Push(const Type& data) {
        auto iter = item_set_.find(data);
        if (iter != item_set_.end()) {
            return false;
        }

        item_set_.insert(data);
        item_queue_.push_back(data);
        while (item_queue_.size() > max_size_) {
            auto old_data = item_queue_.front();
            auto riter = item_set_.find(old_data);
            if (riter != item_set_.end()) {
                item_set_.erase(riter);
            }

            item_queue_.pop_front();
        }

        return true;
    }

private:
    uint32_t max_size_{ 0 };
    std::deque<Type> item_queue_;
    std::unordered_set<Type> item_set_;

    DISALLOW_COPY_AND_ASSIGN(LimitHashSet);
};

};  // namespace common

};  // namespace zjchain