#pragma once

#include <list>
#include <unordered_map>
#include <vector>

#include "common/hash.h"
#include "common/spin_mutex.h"
#include "common/utils.h"
#include "protos/prefix_db.h"
#include "protos/address.pb.h"

namespace shardora {

namespace block {

using AccountPtr = protos::AddressInfoPtr;

template<uint32_t kBucketSize>
class AccountLruMap {
public:
    ~AccountLruMap() {}

    void insert(AccountPtr value) {
        auto& key = value->addr();
        if (item_map_.count(key)) {
            item_list_.erase(item_map_[key]);
            item_map_.erase(key);
        }

        item_list_.push_front(key);
        item_map_[key] = item_list_.begin();
        uint32_t index = common::Hash::Hash32(key) % kBucketSize;
        {
            common::AutoSpinLock spinlock(spin_mutex_);
            index_data_map_[index] = value;
        }

        if (item_list_.size() > kBucketSize) {
            std::string& last = item_list_.back();
            item_map_.erase(last);
            item_list_.pop_back();
        }
    }

    AccountPtr get(const std::string& key) {
        uint32_t index = common::Hash::Hash32(key) % kBucketSize;
        AccountPtr item_ptr = nullptr;
        {
            common::AutoSpinLock spinlock(spin_mutex_);
            item_ptr = index_data_map_[index];
        }
        
        if (item_ptr != nullptr && item_ptr->addr() == key) {
            return item_ptr;
        }
        
        return nullptr;
    }

private:
    std::list<std::string> item_list_;
    std::unordered_map<std::string, typename std::list<std::string>::iterator> item_map_;
    AccountPtr index_data_map_[kBucketSize] = { nullptr };
    common::SpinMutex spin_mutex_;

};

};  // namespace block

};  // namespace shardora
