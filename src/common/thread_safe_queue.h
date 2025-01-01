#pragma once

#include "readerwriterqueue/readerwriterqueue.h"

#include <condition_variable>
#include <mutex>

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
        // auto btime = common::TimeUtils::TimestampUs();
        rw_queue_.enqueue(e);
        auto& tmp_item = *this;
        // assert(size() < 1204);
        CHECK_MEMORY_SIZE(tmp_item);
        // while (!rw_queue_.try_enqueue(e) && !common::GlobalInfo::Instance()->global_stoped()) {
        //     std::unique_lock<std::mutex> lock(mutex_);
        //     con_.wait_for(lock, std::chrono::milliseconds(100));
        // }

        // auto etime = common::TimeUtils::TimestampUs();
        // if (etime - btime > 10) {
        //     ZJC_INFO("push queue use time: %lu", (etime - btime));
        // }
    }

    bool pop(T* e) {
        bool res = rw_queue_.try_dequeue(*e);
        // if (res) {
        //     if (size() >= kQueueCount - 1) {
        //         std::unique_lock<std::mutex> lock(mutex_);
        //         con_.notify_one();
        //     }
        // }

        auto& tmp_item = *this;
        CHECK_MEMORY_SIZE(tmp_item);
        return res;
    }

    size_t size() {
        return rw_queue_.size_approx();
    }

private:
    static const int32_t kQueueCount = 1024;

    moodycamel::ReaderWriterQueue<T, kMaxCount> rw_queue_{kQueueCount};
    std::condition_variable con_;
    std::mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(ThreadSafeQueue);
};

}  // namespace common

}  // namespace shardora
