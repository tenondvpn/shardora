#pragma once

#include <mutex>

#include "common/spin_mutex.h"
#include "tnet/tnet_utils.h"
#include "tnet/event/event_handler.h"
#include "tnet/event/io_event.h"

namespace zjchain {

namespace tnet {

class EpollManager {
public:
    EpollManager();
    ~EpollManager();
    bool Init();
    bool Enable(int sockfd, int type, EventHandler& handler);
    bool Disable(int sockfd, int type, EventHandler& handler);
    int GetEvents(IoEvent* events, int expire);
    bool Wakeup();

private:
    void Destroy();
    void HandleWakeup();

    int epoll_fd_{ 0 };
    bool inited_{ false };
    int wakeup_pipe_[2];
    volatile bool wakeup_event_is_set_{ false };
    mutable common::SpinMutex wakeup_mutex_;

    DISALLOW_COPY_AND_ASSIGN(EpollManager);
};


}  // namespace tnet

}  // namespace zjchain
