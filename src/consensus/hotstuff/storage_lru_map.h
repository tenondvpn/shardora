#pragma once

#include <list>
#include <unordered_map>
#include <vector>

#include "common/hash.h"
#include "common/utils.h"
#include "protos/prefix_db.h"
#include "protos/address.pb.h"

namespace shardora {

namespace hotstuff {

using KeyValuePairPtr = std::shared_ptr<std::pair<std::string, address::protobuf::KeyValueInfo>>;
template<uint32_t kBucketSize>
class StorageLruMap {
public:
    ~StorageLruMap() {}

    void insert(const std::string& key, const address::protobuf::KeyValueInfo& value) {
        auto kv_pair_ptr = std::make_shared<std::pair<std::string, std::string>>(
            std::make_pair(key, value));
        uint32_t index = common::Hash::Hash32(key) % kBucketSize;
        if (item_map_.count(key)) {
            item_list_.erase(item_map_[key]);
            item_map_.erase(key);
            index_data_map_[index] = nullptr;
        }

        item_list_.push_front(key);
        item_map_[key] = item_list_.begin();
        index_data_map_[index] = kv_pair_ptr;
        CHECK_MEMORY_SIZE_WITH_MESSAGE(item_list_, "list");
        CHECK_MEMORY_SIZE_WITH_MESSAGE(item_map_, "map");
        if (item_list_.size() > kBucketSize) {
            std::string& last = item_list_.back();
            item_map_.erase(last);
            item_list_.pop_back();
        }
    }

    KeyValuePairPtr get(const std::string& key) {
        uint32_t index = common::Hash::Hash32(key) % kBucketSize;
        auto item_ptr = index_data_map_[index];
        if (item_ptr != nullptr && item_ptr->first == key) {
            return item_ptr;
        }
        
        return nullptr;
    }

private:
    std::list<std::string> item_list_;
    std::unordered_map<std::string, typename std::list<std::string>::iterator> item_map_;
    KeyValuePairPtr index_data_map_[kBucketSize] = { nullptr };

};

};  // namespace hotstuff

};  // namespace shardora
