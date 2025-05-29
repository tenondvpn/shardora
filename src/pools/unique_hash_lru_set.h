#pragma once

#include <list>
#include <unordered_map>
#include <vector>

#include "common/hash.h"
#include "common/utils.h"
#include "protos/prefix_db.h"
#include "protos/address.pb.h"

namespace shardora {

namespace pools {

template<uint32_t kBucketSize>
class UniqueHashLruSet {
public:
    ~UniqueHashLruSet() {}

    void insert(const std::string& key) {
        uint32_t index = common::Hash::Hash32(key) % kBucketSize;
        if (item_map_.count(key)) {
            item_list_.erase(item_map_[key]);
            item_map_.erase(key);
            index_data_map_[index] = "";
        }

        item_list_.push_front(key);
        item_map_[key] = item_list_.begin();
        index_data_map_[index] = key;
        CHECK_MEMORY_SIZE_WITH_MESSAGE(item_list_, "list");
        CHECK_MEMORY_SIZE_WITH_MESSAGE(item_map_, "map");
        if (item_list_.size() > kBucketSize) {
            std::string& last = item_list_.back();
            item_map_.erase(last);
            item_list_.pop_back();
        }
    }

    bool exists(const std::string& key) {
        uint32_t index = common::Hash::Hash32(key) % kBucketSize;
        auto exists_key = index_data_map_[index];
        if (exists_key == key) {
            return true;
        }
        
        return false;
    }

private:
    std::list<std::string> item_list_;
    std::unordered_map<std::string, typename std::list<std::string>::iterator> item_map_;
    std::string index_data_map_[kBucketSize] = { nullptr };

};

};  // namespace pools

};  // namespace shardora
