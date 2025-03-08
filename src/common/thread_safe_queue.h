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
        assert(size() < 100000lu);
        ZJC_DEBUG("msg queue size: %u", size());
        // if (msg_queue_.size() > 1000) {
        //     return;
        // }

        // std::lock_guard<std::mutex> lock(mutex_);
        // msg_queue_.push(e);
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
        // std::lock_guard<std::mutex> lock(mutex_);
        // if (msg_queue_.empty()) {
        //     return false;
        // }

        // *e = msg_queue_.front();
        // msg_queue_.pop();
        // return true;
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

    size_t size() const {
        return rw_queue_.size_approx();
    }

private:
    static const int32_t kQueueCount = 1024;
    std::queue<T> msg_queue_;

    moodycamel::ReaderWriterQueue<T, kMaxCount> rw_queue_{kQueueCount};
    std::condition_variable con_;
    std::mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(ThreadSafeQueue);
};

}  // namespace common

}  // namespace shardora
