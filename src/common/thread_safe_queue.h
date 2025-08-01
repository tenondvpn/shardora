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
#ifndef NDEBUG
    uint8_t thread_idx = common::GlobalInfo::Instance()->get_thread_index();
           { auto now_thread_id_tmp = std::this_thread::get_id();
            uint32_t now_thread_id = *(uint32_t*)&now_thread_id_tmp;
            ZJC_DEBUG("in timer thread success add thread: %u, maping_thread_idx: %u, "
                "thread_idx: %u, conse thread count: %lu, %p", 
                now_thread_id, 0, thread_idx,
                (common::GlobalInfo::Instance()->message_handler_thread_count() - 2),
                this);
            }
#endif
        bool res = rw_queue_.try_dequeue(*e);
#ifndef NDEBUG
            {auto now_thread_id_tmp = std::this_thread::get_id();
            uint32_t now_thread_id = *(uint32_t*)&now_thread_id_tmp;
            ZJC_DEBUG("out timer thread success add thread: %u, maping_thread_idx: %u, "
                "thread_idx: %u, conse thread count: %lu, %p", 
                now_thread_id, 0, thread_idx,
                (common::GlobalInfo::Instance()->message_handler_thread_count() - 2),
                this);
            }
#endif
        auto& tmp_item = *this;
        CHECK_MEMORY_SIZE(tmp_item);
        return res;
    }

    T* front() {
        assert(false);
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
