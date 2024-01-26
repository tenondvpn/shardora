#include "tnet/event/event_loop.h"

#include <signal.h>

#include "common/global_info.h"
#include "tnet/tnet_utils.h"

namespace zjchain {

namespace tnet {

EventLoop::EventLoop() {}

EventLoop::~EventLoop() {
    Destroy();
}

bool EventLoop::Init() {
    if (epoll_manager_ != NULL) {
        ZJC_ERROR("event loop already init");
        return false;
    }

    epoll_manager_ = new EpollManager;
    if (!epoll_manager_->Init()) {
        delete epoll_manager_;
        epoll_manager_ = nullptr;
        ZJC_ERROR("init epoll event manager failed");
        return false;
    }

    shutdown_ = false;
    return true;
}

void EventLoop::Destroy() {
    if (epoll_manager_ != nullptr) {
        delete epoll_manager_;
        epoll_manager_ = nullptr;
    }
}

bool EventLoop::Shutdown() {
    shutdown_ = true;
    epoll_manager_->Wakeup();
    return true;
}

void EventLoop::Dispatch() {
#ifndef WIN32
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGPIPE);
    sigaddset(&signal_mask, SIGINT);
    int rc = pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
    if (rc != 0) {
        printf("block sigpipe error/n");
    }
#endif        

    IoEvent io_events[kEpollMaxEvents];
    while (!shutdown_) {
        int n = epoll_manager_->GetEvents(io_events, kEpollMaxWaitTime);
        for (int i = 0; i < n; ++i) {
            io_events[i].Process();
        }

        RunTask();
    }
}

bool EventLoop::EnableIoEvent(int sockfd, int type, EventHandler& handler) const {
    return epoll_manager_->Enable(sockfd, type, handler);
}

bool EventLoop::DisableIoEvent(int sockfd, int type, EventHandler& handler) const {
    return epoll_manager_->Disable(sockfd, type, handler);
}

void EventLoop::PostTask(const Task& task) {
    common::AutoSpinLock guard(mutex_);
    task_list_.push_back(task);
}

void EventLoop::Wakeup() const {
    if (epoll_manager_ != nullptr) {
        epoll_manager_->Wakeup();
    }
}

void EventLoop::RunTask() {
    std::deque<Task> task_list;
    {
        common::AutoSpinLock guard(mutex_);
        if (task_list_.empty()) {
            return;
        }

        task_list_.swap(task_list);
    }

    for (auto iter = task_list.begin(); iter != task_list.end(); ++iter) {
        (*iter)();
    }
}

}  // namespace tnet

}  // namespace zjchain
