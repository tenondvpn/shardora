#include "tnet/socket/listen_socket.h"

#include "tnet/socket/server_socket.h"

namespace zjchain {

namespace tnet {

bool ListenSocket::Listen(int backlog) const {
    if (fd_ < 0) {
        ZJC_ERROR("listen on bad fd [%d]", fd_);
        return false;
    }

    if (listen(fd_, backlog) < 0) {
        ZJC_ERROR("listen on fd [%d] failed, errno [%d]", fd_, errno);
        return false;
    }
    return true;
}

bool ListenSocket::Accept(ServerSocket** socket) const
{
    *socket = NULL;
    if (fd_ < 0) {
        ZJC_ERROR("accept on bad fd [%d]", fd_);
        return false;
    }

    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int fd = accept(fd_, (sockaddr*)&addr, &addrlen);
    if (fd < 0) {
        if (errno != EAGAIN) {
            ZJC_ERROR("accept failed [%s]", strerror(errno));
        }

        return false;
    }

    ServerSocket* server_socket = new ServerSocket(
            fd,
            addr.sin_addr.s_addr,
            ntohs(addr.sin_port),
            local_addr_,
            local_port_);
    if (server_socket == nullptr) {
        ZJC_ERROR("create tcp server socket failed");
        close(fd);
        return false;
    }

    *socket = server_socket;
    return true;
}

}  // namespace tnet

}  // namespace zjchain
