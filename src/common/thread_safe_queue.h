#pragma once

#include "readerwriterqueue/readerwriterqueue.h"

#include <condition_variable>
#include <mutex>

#include "common/log.h"

namespace zjchain {

namespace common {

template<class T, uint32_t kMaxCount=1024 * 1024>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() {}

    ~ThreadSafeQueue() {}

    void push(T e) {
        while (!rw_queue_.try_enqueue(e)) {
            ZJC_DEBUG("get rw_queue size: %u", size());
            std::unique_lock<std::mutex> lock(mutex_);
            con_.wait_for(lock, std::chrono::milliseconds(100));
        }
    }

    bool pop(T* e) {
        bool res = rw_queue_.try_dequeue(*e);
        if (res) {
            con_.notify_one();
        }

        return res;
    }

    size_t size() {
        return rw_queue_.size_approx();
    }

private:
    moodycamel::ReaderWriterQueue<T, kMaxCount> rw_queue_;
    std::condition_variable con_;
    std::mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(ThreadSafeQueue);
};

}  // namespace common

}  // namespace zjchain
