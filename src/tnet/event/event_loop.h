#pragma once

#include <deque>
#include <functional>

#include "common/spin_mutex.h"
#include "tnet/event/epoll_manager.h"

namespace zjchain {

namespace tnet {

class EventLoop {
public:
    typedef std::function<void()> Task;

    EventLoop();
    ~EventLoop();
    bool Init();
    void Destroy();
    bool Shutdown();
    void Dispatch();
    bool EnableIoEvent(int sockfd, int type, EventHandler& handler) const;
    bool DisableIoEvent(int sockfd, int type, EventHandler& handler) const;
    void PostTask(const Task& task);
    void Wakeup() const;

private:
    void RunTask();

    EpollManager* epoll_manager_{ nullptr };
    mutable common::SpinMutex mutex_;
    std::deque<Task> task_list_;
    volatile bool shutdown_{ false };

    DISALLOW_COPY_AND_ASSIGN(EventLoop);
};

}  // namespace tnet

}  // namespace zjchain
