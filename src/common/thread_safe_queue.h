#pragma once

#include "readerwriterqueue/readerwriterqueue.h"

namespace zjchain {

namespace common {

template<class T, uint32_t kMaxCount=1024*1024>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() {}

    ~ThreadSafeQueue() {}

    void push(T e) {
        rw_queue_.enqueue(e);
    }

    bool pop(T* e) {
        return rw_queue_.try_dequeue(*e);
    }

    size_t size() {
        return rw_queue_.size_approx();
    }

private:
    moodycamel::ReaderWriterQueue<T, kMaxCount> rw_queue_;

    DISALLOW_COPY_AND_ASSIGN(ThreadSafeQueue);
};

}  // namespace common

}  // namespace zjchain
