#pragma once

#include "readerwriterqueue/readerwriterqueue.h"

#include <condition_variable>
#include <mutex>
#include <queue>

#include "common/global_info.h"
#include "common/log.h"

namespace shardora {

namespace common {

template<class T, uint32_t kMaxCount=1024>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() {}

    ~ThreadSafeQueue() {}

    void push(T e) {
        rw_queue_.enqueue(e);
        auto& tmp_item = *this;
        CHECK_MEMORY_SIZE(tmp_item);
    }

    bool pop(T* e) {
        bool res = rw_queue_.try_dequeue(*e);
        auto& tmp_item = *this;
        CHECK_MEMORY_SIZE(tmp_item);
        return res;
    }

    size_t size() const {
        return rw_queue_.size_approx();
    }

private:
    static const int32_t kQueueCount = 1024;
    moodycamel::ReaderWriterQueue<T, kMaxCount> rw_queue_{kQueueCount};

    DISALLOW_COPY_AND_ASSIGN(ThreadSafeQueue);
};

}  // namespace common

}  // namespace shardora
