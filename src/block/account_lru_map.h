#pragma once

#include <list>
#include <mutex>
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
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& key = value->addr();
        auto it = value_map_.find(key);
        if (it != value_map_.end()) {
            // Key exists: remove from LRU list and update value.
            auto map_it = item_map_.find(key);
            if (map_it != item_map_.end()) {
                item_list_.erase(map_it->second);
                item_map_.erase(map_it);
            }
            value_map_.erase(it);
        }

        item_list_.push_front(key);
        item_map_[key] = item_list_.begin();
        value_map_[key] = value;

        if (item_list_.size() > kBucketSize) {
            const std::string& last = item_list_.back();
            item_map_.erase(last);
            value_map_.erase(last);
            item_list_.pop_back();
        }

    }

    AccountPtr get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = value_map_.find(key);
        if (it != value_map_.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Atomic get-or-insert: returns existing value for key, or inserts
    // the provided value and returns it.  Avoids the TOCTOU window
    // between a separate get() + insert() pair that lets multiple
    // threads race through the DB-lookup path for the same key.
    AccountPtr get_or_insert(const std::string& key, AccountPtr value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = value_map_.find(key);
        if (it != value_map_.end()) {
            return it->second;
        }

        // Not present — insert the caller-supplied value.
        item_list_.push_front(key);
        item_map_[key] = item_list_.begin();
        value_map_[key] = value;

        if (item_list_.size() > kBucketSize) {
            const std::string& last = item_list_.back();
            item_map_.erase(last);
            value_map_.erase(last);
            item_list_.pop_back();
        }

        return value;
    }

private:
    std::list<std::string> item_list_;
    std::unordered_map<std::string, typename std::list<std::string>::iterator> item_map_;
    std::unordered_map<std::string, AccountPtr> value_map_;
    std::mutex mutex_;
};

};  // namespace block

};  // namespace shardora
