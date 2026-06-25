#pragma once

#include <unordered_map>
#include <list>
#include <utility>

#include "common/utils.h"

namespace shardora {

namespace common {

/**
 * LRU (Least Recently Used) Map Implementation
 * * Features:
 * - Supports key-value storage
 * - Automatically evicts the least recently used items
 * - O(1) lookup, insertion, and deletion
 * - Not thread-safe, requires external synchronization
 * * Usage:
 * LRUMap<std::string, int> cache(100);
 * cache.Put("key1", 42);
 * auto val = cache.Get("key1");
 * cache.Remove("key1");
 */
template<class Key, class Value>
class LRUMap {
public:
    using KVPair = std::pair<const Key, Value>;
    using ListIterator = typename std::list<KVPair>::iterator;
    using MapType = std::unordered_map<Key, ListIterator>;

    explicit LRUMap(uint32_t max_size) : max_size_(max_size) {
        if (max_size <= 0) {
            max_size_ = 1;  // Maintain a minimum capacity of 1
        }
    }

    ~LRUMap() {
        Clear();
    }

    /**
     * Checks if the key exists
     */
    bool Contains(const Key& key) const {
        return item_map_.count(key) > 0;
    }

    /**
     * Gets the value corresponding to the key
     * @return Returns true if exists (value returned via parameter); otherwise returns false
     */
    bool Get(const Key& key, Value& value) {
        auto it = item_map_.find(key);
        if (it == item_map_.end()) {
            return false;
        }

        // Move the accessed item to the front (most recently used)
        ListIterator list_it = it->second;
        auto item = *list_it;
        item_list_.erase(list_it);
        item_list_.push_front(item);
        
        // Update iterator
        item_map_[key] = item_list_.begin();
        value = item_list_.begin()->second;
        
        return true;
    }

    /**
     * Gets the value corresponding to the key (const version, does not modify LRU order)
     * @return Returns true if exists (value returned via parameter); otherwise returns false
     */
    bool Peek(const Key& key, Value& value) const {
        auto it = item_map_.find(key);
        if (it == item_map_.end()) {
            return false;
        }

        value = it->second->second;
        return true;
    }

    /**
     * Inserts or updates a key-value pair
     * @return Returns true if newly inserted, otherwise returns false (indicating update)
     */
    bool Put(const Key& key, const Value& value) {
        auto it = item_map_.find(key);
        
        if (it != item_map_.end()) {
            // Key already exists, update value and move to front
            ListIterator list_it = it->second;
            item_list_.erase(list_it);
            item_list_.push_front(KVPair(key, value));
            item_map_[key] = item_list_.begin();
            return false;
        }

        // New key-value pair
        item_list_.push_front(KVPair(key, value));
        item_map_[key] = item_list_.begin();

        // If max capacity is exceeded, remove the least recently used item (the last one)
        if (item_list_.size() > max_size_) {
            const Key& last_key = item_list_.back().first;
            item_map_.erase(last_key);
            item_list_.pop_back();
        }

        return true;
    }

    /**
     * Removes the specified key
     * @return Returns true if removed successfully, false if key does not exist
     */
    bool Remove(const Key& key) {
        auto it = item_map_.find(key);
        if (it == item_map_.end()) {
            return false;
        }

        ListIterator list_it = it->second;
        item_list_.erase(list_it);
        item_map_.erase(it);
        return true;
    }

    /**
     * Gets the number of currently stored items
     */
    size_t Size() const {
        return item_map_.size();
    }

    /**
     * Gets the maximum capacity
     */
    uint32_t GetMaxSize() const {
        return max_size_;
    }

    /**
     * Sets the maximum capacity
     * If the new capacity is smaller than the current size, the least recently used items will be removed
     */
    void SetMaxSize(uint32_t max_size) {
        if (max_size <= 0) {
            max_size_ = 1;
        } else {
            max_size_ = max_size;
        }

        // If exceeding the new max capacity, remove excess items
        while (item_list_.size() > max_size_) {
            const Key& last_key = item_list_.back().first;
            item_map_.erase(last_key);
            item_list_.pop_back();
        }
    }

    /**
     * Clears all items
     */
    void Clear() {
        item_list_.clear();
        item_map_.clear();
    }

    /**
     * Gets the least recently used (LRU) key
     * @return Returns true if map is not empty (key returned via parameter); otherwise returns false
     */
    bool GetLRUKey(Key& key) const {
        if (item_list_.empty()) {
            return false;
        }
        key = item_list_.back().first;
        return true;
    }

    /**
     * Gets the most recently used (MRU) key (newest access)
     * @return Returns true if map is not empty (key returned via parameter); otherwise returns false
     */
    bool GetMRUKey(Key& key) const {
        if (item_list_.empty()) {
            return false;
        }
        key = item_list_.front().first;
        return true;
    }

    /**
     * Iterates over all items (from most recent to least recent)
     * The caller-provided callback function returns true to continue iteration, false to stop
     */
    template<typename Callback>
    void ForEach(Callback callback) const {
        for (const auto& pair : item_list_) {
            if (!callback(pair.first, pair.second)) {
                break;
            }
        }
    }

    /**
     * Gets the status information of the LRU map
     */
    std::string GetStats() const {
        std::string stats = "LRUMap[";
        stats += "size=" + std::to_string(Size());
        stats += ", max_size=" + std::to_string(GetMaxSize());
        stats += "]";
        return stats;
    }

private:
    uint32_t max_size_{0};
    std::list<KVPair> item_list_;  // From front to back: Newest -> Oldest
    MapType item_map_;             // key → list iterator mapping

    // DISALLOW_COPY_AND_ASSIGN(LRUMap<Key, Value>);
};

}  // namespace common

}  // namespace shardora