#include "tnet/event/epoll_manager.h"

#include <sys/epoll.h>
#include <unistd.h>

namespace zjchain {

namespace tnet {

EpollManager::EpollManager() {
    wakeup_pipe_[0] = -1;
    wakeup_pipe_[1] = -1;
}

EpollManager::~EpollManager() {
    Destroy();
}

bool EpollManager::Init() {
    if (inited_) {
        ZJC_ERROR("event manager has been inited");
        return false;
    }

    epoll_fd_ = epoll_create(10240);
    if (epoll_fd_ >= 0) {
        if (pipe(wakeup_pipe_)) {
            ZJC_ERROR("pipe failed [%s]", strerror(errno));
        } else {
            inited_ = true;
        }
    } else {
        ZJC_ERROR("epoll_create failed [%s]", strerror(errno));
    }

    return inited_;
}

bool EpollManager::Enable(int sockfd, int type, EventHandler& handler) {
    if (!inited_) {
        ZJC_ERROR("event manager has not been inited");
        return false;
    }

    int old_ev_type = handler.event_type();
    int op = old_ev_type == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    int old_events = 0;
    if (old_ev_type & kEventRead) {
        old_events |= EPOLLIN;
    }

    if (old_ev_type & kEventWrite) {
        old_ev_type |= EPOLLOUT;
    }

    int new_events = 0;
    if (type & kEventRead) {
        new_events |= EPOLLIN;
    }

    if (type & kEventWrite) {
        new_events |= EPOLLOUT;
    }

    if ((new_events & old_events) == new_events) {
        ZJC_ERROR("already enabled");
        return true;
    }

    int events = old_events | new_events;
    if (events == 0) {
        ZJC_ERROR("no events exist");
        return false;
    }

    epoll_event ev = { 0 };
    ev.data.ptr = &handler;
    ev.events = events | EPOLLET;
    if (epoll_ctl(epoll_fd_, op, sockfd, &ev) < 0) {
        ZJC_ERROR("epoll_ctl failed on sock [%d] [%s]", sockfd, strerror(errno));
        return false;
    }

    handler.set_event_type(old_ev_type | type);
    return true;
}

bool EpollManager::Disable(int sockfd, int type, EventHandler& handler) {
    if (!inited_) {
        ZJC_ERROR("event manager has not been inited");
        return false;
    }

    int old_event_type = handler.event_type();
    int old_events = 0;
    if (old_event_type & kEventRead) {
        old_events |= EPOLLIN;
    }

    if (old_event_type & kEventWrite) {
        old_events |= EPOLLOUT;
    }

    int new_events = 0;
    if (type & kEventRead) {
        new_events |= EPOLLIN;
    }

    if (type & kEventWrite) {
        new_events |= EPOLLOUT;
    }

    if ((new_events & old_events) != new_events) {
        return false;
    }

    int events = old_events & ~new_events;
    int op = events == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
    epoll_event ev = { 0 };
    ev.data.ptr = &handler;
    ev.events = events | EPOLLET;
    if (epoll_ctl(epoll_fd_, op, sockfd, &ev) < 0) {
        ZJC_ERROR("epoll_ctl failed on sock[%d] [%s]", sockfd, strerror(errno));
        return false;
    }

    handler.set_event_type(old_event_type & ~type);
    return true;
}

int EpollManager::GetEvents(IoEvent* events, int expire) {
    if (!inited_) {
        ZJC_ERROR("event manager has not been inited");
        return false;
    }

    epoll_event epoll_events[kDvMaxEvents];
    int n = epoll_wait(epoll_fd_, epoll_events, kDvMaxEvents, expire);
    if (n < 0) {
        if (errno != EINTR) {
            ZJC_ERROR("epoll_wait failed [%s]", strerror(errno));
        }

        return 0;
    }

    for (int i = 0; i < n; i++) {
        IoEvent& event = events[i];
        event.Reset();
        void* ptr = epoll_events[i].data.ptr;
        int events = epoll_events[i].events;
        if (ptr == this) {
            HandleWakeup();
            continue;
        }

        EventHandler* handler = reinterpret_cast<EventHandler*>(ptr);
        if ((events & (EPOLLERR | EPOLLHUP)) != 0 && (events & (EPOLLIN | EPOLLOUT)) == 0) {
            events |= EPOLLIN | EPOLLOUT;
        }

        int flags = 0;
        int eventType = handler->event_type();
        if ((events & EPOLLIN) == EPOLLIN && (eventType & kEventRead) == kEventRead) {
            flags |= kEventRead;
        }

        if ((events & EPOLLOUT) == EPOLLOUT && (eventType & kEventWrite) == kEventWrite) {
            flags |= kEventWrite;
        }

        event.SetType(flags);
        event.SetHandler(handler);
    }

    return n;
}

bool EpollManager::Wakeup() {
    if (!inited_) {
        ZJC_ERROR("event manager has not been inited");
        return false;
    }

    common::AutoSpinLock guard(wakeup_mutex_);
    if (wakeup_event_is_set_) {
        return true;
    }

    epoll_event ev;
    ev.data.ptr = this;
    ev.events = EPOLLOUT;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_pipe_[1], &ev) != 0) {
        ZJC_ERROR("epoll_ctl failed [%s]", strerror(errno));
        return false;
    }

    wakeup_event_is_set_ = true;
    return true;
}

void EpollManager::Destroy() {
    if (wakeup_pipe_[0] >= 0) {
        if (close(wakeup_pipe_[0])) {
            ZJC_ERROR("close pipe fd failed [%s]", strerror(errno));
        }

        wakeup_pipe_[0] = -1;
    }

    if (wakeup_pipe_[1] >= 0) {
        if (close(wakeup_pipe_[1])) {
            ZJC_ERROR("close pipe fd failed [%s]", strerror(errno));
        }

        wakeup_pipe_[1] = -1;
    }

    if (epoll_fd_ >= 0) {
        if (close(epoll_fd_)) {
            ZJC_ERROR("close epoll fd failed [%s]", strerror(errno));
        }

        epoll_fd_ = -1;
    }

    inited_ = false;
}

void EpollManager::HandleWakeup() {
    common::AutoSpinLock guard(wakeup_mutex_);
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    wakeup_event_is_set_ = false;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, wakeup_pipe_[1], &ev) < 0) {
        ZJC_ERROR("epoll_ctl failed [%s]", strerror(errno));
    }
}

}  // namespace tnet

}  // namespace zjchain
