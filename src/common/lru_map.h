#pragma once

#include <unordered_map>
#include <list>
#include <utility>

#include "common/utils.h"

namespace shardora {

namespace common {

/**
 * LRU (Least Recently Used) Map 实现
 * 
 * 特点:
 *   - 支持 key-value 存储
 *   - 自动淘汰最久未使用的项目
 *   - O(1) 查找、插入、删除
 *   - 线程不安全，需要外部同步
 * 
 * 用法:
 *   LRUMap<std::string, int> cache(100);
 *   cache.Put("key1", 42);
 *   auto val = cache.Get("key1");
 *   cache.Remove("key1");
 */
template<class Key, class Value>
class LRUMap {
public:
    using KVPair = std::pair<const Key, Value>;
    using ListIterator = typename std::list<KVPair>::iterator;
    using MapType = std::unordered_map<Key, ListIterator>;

    explicit LRUMap(uint32_t max_size) : max_size_(max_size) {
        if (max_size <= 0) {
            max_size_ = 1;  // 最少保持 1 个容量
        }
    }

    ~LRUMap() {
        Clear();
    }

    /**
     * 检查 key 是否存在
     */
    bool Contains(const Key& key) const {
        return item_map_.count(key) > 0;
    }

    /**
     * 获取 key 对应的值
     * @return 如果存在返回 true，通过 value 参数返回值；否则返回 false
     */
    bool Get(const Key& key, Value& value) {
        auto it = item_map_.find(key);
        if (it == item_map_.end()) {
            return false;
        }

        // 将访问的项目移到前面（最新）
        ListIterator list_it = it->second;
        auto item = *list_it;
        item_list_.erase(list_it);
        item_list_.push_front(item);
        
        // 更新迭代器
        item_map_[key] = item_list_.begin();
        value = item_list_.begin()->second;
        
        return true;
    }

    /**
     * 获取 key 对应的值（const 版本，不修改 LRU 顺序）
     * @return 如果存在返回 true，通过 value 参数返回值；否则返回 false
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
     * 插入或更新 key-value 对
     * @return 如果是新插入返回 true，否则返回 false（表示更新）
     */
    bool Put(const Key& key, const Value& value) {
        auto it = item_map_.find(key);
        
        if (it != item_map_.end()) {
            // key 已存在，更新值并移到前面
            ListIterator list_it = it->second;
            item_list_.erase(list_it);
            item_list_.push_front(KVPair(key, value));
            item_map_[key] = item_list_.begin();
            // CHECK_MEMORY_SIZE(item_list_);
            return false;
        }

        // 新的 key-value 对
        item_list_.push_front(KVPair(key, value));
        item_map_[key] = item_list_.begin();
        // CHECK_MEMORY_SIZE(item_list_);

        // 如果超过最大容量，删除最久未使用的项目（最后一个）
        if (item_list_.size() > max_size_) {
            const Key& last_key = item_list_.back().first;
            item_list_.pop_back();
            item_map_.erase(last_key);
            // CHECK_MEMORY_SIZE(item_list_);
        }

        return true;
    }

    /**
     * 删除指定 key
     * @return 如果删除成功返回 true，key 不存在返回 false
     */
    bool Remove(const Key& key) {
        auto it = item_map_.find(key);
        if (it == item_map_.end()) {
            return false;
        }

        ListIterator list_it = it->second;
        item_list_.erase(list_it);
        item_map_.erase(it);
        // CHECK_MEMORY_SIZE(item_list_);
        return true;
    }

    /**
     * 获取当前存储的项目数量
     */
    size_t Size() const {
        return item_map_.size();
    }

    /**
     * 获取最大容量
     */
    uint32_t GetMaxSize() const {
        return max_size_;
    }

    /**
     * 设置最大容量
     * 如果新容量小于当前大小，会删除最久未使用的项目
     */
    void SetMaxSize(uint32_t max_size) {
        if (max_size <= 0) {
            max_size_ = 1;
        } else {
            max_size_ = max_size;
        }

        // 如果超过新的最大容量，删除多余的项目
        while (item_list_.size() > max_size_) {
            const Key& last_key = item_list_.back().first;
            item_list_.pop_back();
            item_map_.erase(last_key);
        }
        // CHECK_MEMORY_SIZE(item_list_);
    }

    /**
     * 清空所有项目
     */
    void Clear() {
        item_list_.clear();
        item_map_.clear();
    }

    /**
     * 获取最近最久使用的 key
     * @return 如果 map 非空返回 true，通过 key 参数返回；否则返回 false
     */
    bool GetLRUKey(Key& key) const {
        if (item_list_.empty()) {
            return false;
        }
        key = item_list_.back().first;
        return true;
    }

    /**
     * 获取最近最常使用的 key（最新访问）
     * @return 如果 map 非空返回 true，通过 key 参数返回；否则返回 false
     */
    bool GetMRUKey(Key& key) const {
        if (item_list_.empty()) {
            return false;
        }
        key = item_list_.front().first;
        return true;
    }

    /**
     * 遍历所有项目（从最新到最久）
     * 调用者提供的 callback 函数返回 true 继续遍历，返回 false 停止
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
     * 获取 LRU map 的状态信息
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
    std::list<KVPair> item_list_;  // 从前到后: 最新 → 最久
    MapType item_map_;              // key → list iterator 映射

    // DISALLOW_COPY_AND_ASSIGN(LRUMap<Key, Value>);
};

}  // namespace common

}  // namespace shardora
