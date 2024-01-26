#include "tnet/socket/socket.h"

#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace zjchain {

namespace tnet {

Socket::Socket() {}

Socket::~Socket() {
    Close();
}

int Socket::Read(void* buf, size_t len) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return -1;
    }

    int n = 0;
    while (true) {
        n = read(fd_, buf, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
        }

        break;
    }

    return n;
}

int Socket::Write(const void* buf, size_t len) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return -1;
    }

    int n = 0;
    while (true) {
        n = send(fd_, buf, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
        }

        break;
    }

    return n;
}

void Socket::Close() {
    if (fd_ >= 0) {
        if (close(fd_) < 0) {
            ZJC_ERROR("close fd [%d] failed [%s]", fd_, strerror(errno));
        }

        fd_ = -1;
    }
}

void Socket::ShutdownWrite() {
    if (fd_ >= 0) {
        shutdown(fd_, SHUT_WR);
    }
}

void Socket::ShutdownRead() {
    if (fd_ >= 0) {
        shutdown(fd_, SHUT_RD);
    }
}

bool Socket::SetNonBlocking(bool enable) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }

    int flags = 0;
    if ((flags = fcntl(fd_, F_GETFL)) == -1) {
        ZJC_ERROR("fcntl fd [%d] F_GETFL failed [%s]", fd_, strerror(errno));
        return false;
    }

    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(fd_, F_SETFL, flags) == -1) {
        ZJC_ERROR("fcntl fd [%d] F_SETFL failed [%s]", fd_, strerror(errno));
        return false;
    }
    return true;
}

bool Socket::SetCloseExec(bool enable) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }

    int flags = 0;
    if ((flags = fcntl(fd_, F_GETFD)) == -1) {
        ZJC_ERROR("fcntl fd [%d] F_GETFD failed [%s]", fd_, strerror(errno));
        return false;
    }

    if (enable) {
        flags |= FD_CLOEXEC;
    } else {
        flags &= ~FD_CLOEXEC;
    }

    if (fcntl(fd_, F_SETFD, flags) == -1) {
        ZJC_ERROR("fcntl fd [%d] F_SETFD failed [%s]", fd_, strerror(errno));
        return false;
    }

    return true;
}

bool Socket::SetTcpNoDelay(bool enable) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }

    int noDelay = enable ? 1 : 0;
    if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay)) < 0) {
        ZJC_ERROR("setsockopt failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    return true;
}

bool Socket::SetSoLinger(bool enable, int seconds) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }

    linger lingerValue;
    lingerValue.l_onoff = enable ? 1 : 0;
    lingerValue.l_linger = seconds;

    if (setsockopt(fd_, SOL_SOCKET, SO_LINGER, &lingerValue, sizeof(lingerValue)) < 0) {
        ZJC_ERROR("setsockopt failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    return true;
}

bool Socket::SetTcpKeepAlive(int idleTime, int keepInterval, int cnt) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }

    if (setsockopt(fd_, SOL_TCP, TCP_KEEPIDLE, &idleTime, 
                   sizeof(idleTime)) < 0) {
        ZJC_ERROR("setsockopt TCP_KEEPIDLE failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    if (setsockopt(fd_, SOL_TCP, TCP_KEEPINTVL, &keepInterval, 
                   sizeof(keepInterval)) < 0) {
        ZJC_ERROR("setsockopt TCP_KEEPINTVL failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    if (setsockopt(fd_, SOL_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)) < 0) {
        ZJC_ERROR("setsockopt TCP_KEEPCNT failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    return true;
}

bool Socket::SetSoRcvBuf(int buffSize) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }

    if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &buffSize, sizeof(buffSize)) < 0) {
        ZJC_ERROR("setsockopt failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    return true;
}

bool Socket::SetSoSndBuf(int buffSize) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }

    if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &buffSize, sizeof(buffSize)) < 0) {
        ZJC_ERROR("setsockopt failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    return true;
}

bool Socket::SetOption(int option, const void* value, size_t len) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }

    if (setsockopt(fd_, SOL_SOCKET, option, value, len) < 0) {
        ZJC_ERROR("setsockopt failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    return true;
}

bool Socket::GetSoError(int* code) const {
    if (fd_ < 0) {
        ZJC_ERROR("bad fd [%d]", fd_);
        return false;
    }
    socklen_t codeLen = sizeof(*code);
    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, code, &codeLen) < 0) {
        ZJC_ERROR("getsockopt failed on fd [%d] [%s]", fd_, strerror(errno));
        return false;
    }

    if (codeLen != sizeof(*code)) {
        ZJC_ERROR("result size not match");
        return false;
    }

    return true;
}

void Socket::Free() {
    delete this;
}

int Socket::GetIpPort(std::string* ip, uint16_t* port) {
    if (fd_ == 0) {
        return -1;
    }

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd_, (struct sockaddr*)&addr, &addrlen) == -1) {
        return -1;
    }

    char tmp_ip[INET6_ADDRSTRLEN];
    memset(tmp_ip, 0, sizeof(tmp_ip));
    if ((inet_ntop(addr.sin_family, &addr.sin_addr, tmp_ip, INET6_ADDRSTRLEN)) == NULL) {
        return -1;
    }

    *ip = tmp_ip;
    *port = ntohs(addr.sin_port);
    return 0;
}

}  // namespace tnet

}  // namespace zjchain
