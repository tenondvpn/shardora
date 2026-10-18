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
    }

    // Non-allocating enqueue.  The underlying push() maps to moodycamel's
    // enqueue(), which appends a new block whenever the current one is full and
    // never frees blocks again -- a queue used as a retry buffer therefore grows
    // without bound.  Producers that can tolerate a dropped element should use
    // this instead: it returns false once the reserved capacity is exhausted
    // rather than allocating, so the caller decides what a drop means (for a
    // sync request, the next round simply re-asks).
    bool TryPush(T e) {
        return rw_queue_.try_enqueue(e);
    }

    bool pop(T* e) {
        bool res = rw_queue_.try_dequeue(*e);
        auto& tmp_item = *this;
        return res;
    }

    T* front() {
        return rw_queue_.peek();
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
