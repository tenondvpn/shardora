#pragma once

#include <iostream>

namespace zjchain {

namespace common {

template<typename T, uint8_t Capacity>
class FixedQueue {
public:
    FixedQueue() : front_(0), rear_(0), size_(0) {}

    void Enqueue(const T& value) {
        if (IsFull()) {
            return;
        }

        data_[rear_] = value;
        ++rear_;
        if (rear_ == Capacity) {
            rear_ = 0;
        }

        ++size_;
    }

    void Dequeue() {
        if (IsEmpty()) {
            return;
        }

        ++front_;
        if (front_ == Capacity) {
            front_ = 0;
        }

        --size_;
    }

    inline const T& Front() const {
        if (IsEmpty()) {
            static const T empty_value{};
            return empty_value;
        }
        return data_[front_];
    }

    inline const T& Rear() const {
        if (IsEmpty()) {
            static const T empty_value{};
            return empty_value;
        }
        const uint8_t rear_index = rear_ == 0 ? Capacity - 1 : rear_ - 1;
        return data_[rear_index];
    }

    inline bool IsEmpty() const { return size_ == 0; }

    inline bool IsFull() const { return size_ == Capacity; }

    bool Exists(const T& val) {
        if (IsEmpty()) {
            return false;
        }

        uint8_t i = front_;
        for (; i < rear_ && i < Capacity; ++i) {
            if (data_[i] == val) {
                return true;
            }
        }

        if (i == rear_) {
            return false;
        }

        if (i == Capacity) {
            i = 0;
        }

        for (; i < rear_; ++i) {
            if (data_[i] == val) {
                return true;
            }
        }

        return false;
    }

    uint8_t Size() const { return size_; }

    T data_[Capacity];
    uint8_t front_;
    uint8_t rear_;
    uint8_t size_;
};


}  // namespace common

}  // namespace zjchain
