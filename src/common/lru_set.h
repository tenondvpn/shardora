#pragma once

#include <unordered_set>
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
        auto iter = item_set_.find(data);
        if (iter != item_set_.end()) {
            return true;
        }

        return false;
    }

    bool Push(const Type& data) {
        
    }

    void Reset() {
        item_list_.clear();
        item_set_.clear();
    }

private:
    uint32_t max_size_{ 0 };
    std::list<Type> item_list_;
    std::unordered_set<Type> item_set_;

    DISALLOW_COPY_AND_ASSIGN(LRUSet<Type>);
};

};  // namespace common

};  // namespace zjchain
