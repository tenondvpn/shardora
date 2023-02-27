#pragma once

#include <cstdint>
#include <unordered_set>
#include "common/hash.h"

namespace zjchain {

namespace common {

template<class Type>
uint64_t MinHeapUniqueVal(const Type& val) {
    return common::Hash::Hash64(val);
}

template<>
uint64_t MinHeapUniqueVal(const std::string& val);

template<>
uint64_t MinHeapUniqueVal(const uint64_t& val);

template<>
uint64_t MinHeapUniqueVal(const int64_t& val);

template<>
uint64_t MinHeapUniqueVal(const uint32_t& val);

template<>
uint64_t MinHeapUniqueVal(const int32_t& val);

template <class Type>
class LimitHeap {
public:
    LimitHeap(bool unique, uint32_t max_size)
            : unique_(unique), max_size_(max_size) {
        data_ = new Type[max_size_];
    }

    ~LimitHeap() {
        delete[] data_;
    }

    LimitHeap(const LimitHeap &other) {
        max_size_ = other.max_size_;
        data_ = new Type[max_size_];
        for (uint32_t i = 0; i < other.size_; ++i) {
            data_[i] = other.data_[i];
        }

        size_ = other.size_;
    }

    LimitHeap& operator=(const LimitHeap &other) {
        if (this == &other) {
            return *this;
        }

        for (uint32_t i = 0; i < other.size_; ++i) {
            data_[i] = other.data_[i];
        }

        size_ = other.size_;
        return *this;
    }

    inline int32_t push(Type val) {
        if (size_ >= max_size_ && OperaterMinOrMax(val, data_[0])) {
            ZJC_ERROR("min heap push failed![%d] is max heap: %d",
                OperaterMinOrMax(val, data_[0]));
            return -1;
        }

        if (unique_) {
            if (unique_set_.find(MinHeapUniqueVal(val)) != unique_set_.end()) {
                return -1;
            }

            unique_set_.insert(MinHeapUniqueVal(val));
        }

        if (size_ >= max_size_) {
            pop();
        }

        data_[size_] = val;
        ++size_;
        return AdjustUp(size_ - 1);
    }

    inline void pop() {
        if (unique_) {
            unique_set_.erase(MinHeapUniqueVal(data_[0]));
        }

        Swap(0, size_ - 1);
        --size_;
        AdjustDown(0);
    }

    inline Type top() {
        return data_[0];
    }

    inline bool empty() {
        return size_ == 0;
    }

    inline Type* data() {
        return data_;
    }

    inline uint32_t size() {
        return size_;
    }

    int32_t AdjustDown(int32_t index) {
        while (true) {
            int32_t l_child = LeftChild(index);
            if (l_child >= size_) {
                break;
            }

            int32_t r_child = RightChild(index);
            if (r_child >= size_) {
                if (!OperaterMinOrMax(data_[l_child], data_[index])) {
                    break;
                }

                Swap(l_child, index);
                index = l_child;
                continue;
            }

            if (OperaterMinOrMax(data_[index], data_[r_child]) &&
                    OperaterMinOrMax(data_[index], data_[l_child])) {
                break;
            }

            if (OperaterMinOrMax(data_[l_child], data_[r_child])) {
                Swap(l_child, index);
                index = l_child;
            } else {
                Swap(r_child, index);
                index = r_child;
            }
        }

        return index;
    }

    int32_t AdjustUp(int32_t index) {
        while (index > 0) {
            int32_t parent_idx = ParentIndex(index);
            if (!OperaterMinOrMax(data_[index], data_[parent_idx])) {
                break;
            }

            Swap(index, parent_idx);
            index = parent_idx;
        }

        return index;
    }

    inline int32_t LeftChild(int32_t index) {
        return 2 * index + 1;
    }

    inline int32_t RightChild(int32_t index) {
        return 2 * index + 2;
    }

    inline int32_t ParentIndex(int32_t index) {
        if (index % 2 == 0) {
            return index / 2 - 1;
        }

        return index / 2;
    }

    inline void Swap(int32_t l, int32_t r) {
        Type tmp_val = data_[l];
        data_[l] = data_[r];
        data_[r] = tmp_val;
    }

    inline bool OperaterMinOrMax(Type left, Type right) {
        return left < right;
    }

    Type* data_{ nullptr };
    int32_t size_{ 0 };
    bool unique_{ false };
    std::unordered_set<uint64_t> unique_set_;
    uint32_t max_size_{ -1 };
};

}  // namespace common

}  // namespace zjchain
