#include <iostream>
#include <queue>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace shardora {

namespace common {

/**
 * @brief Template-based Unique Min-Priority Queue
 * @tparam T The type of elements stored in the queue
 */
template <typename T>
class UniqueMinPriorityQueue {
public:
    // Insert with duplication check: O(log N) + O(1)
    void push(const T& val) {
        // Use unordered_set for O(1) average time complexity lookup
        if (exists_.find(val) == exists_.end()) {
            pq_.push(val);
            exists_.insert(val);
        }
    }

    // Remove the top (smallest) element
    void pop() {
        if (!pq_.empty()) {
            exists_.erase(pq_.top()); // Sync removal from the set
            pq_.pop();
        }
    }

    // Access the top (smallest) element
    const T& top() const {
        return pq_.top();
    }

    bool empty() const {
        return pq_.empty();
    }

    size_t size() const {
        return pq_.size();
    }

private:
    // Min-heap: std::greater ensures smallest values come first
    std::priority_queue<T, std::vector<T>, std::greater<T>> pq_;
    // Set for uniqueness tracking
    std::unordered_set<T> exists_;
};

}  // namespace common

}  // namespace shardora