#include "tnet/socket/listen_socket.h"

#include "tnet/socket/server_socket.h"

namespace shardora {

namespace tnet {

bool ListenSocket::Listen(int backlog) const {
    if (fd_ < 0) {
        SHARDORA_ERROR("listen on bad fd [%d]", fd_);
        return false;
    }

    if (listen(fd_, backlog) < 0) {
        SHARDORA_ERROR("listen on fd [%d] failed, errno [%d]", fd_, errno);
        return false;
    }
    return true;
}

std::shared_ptr<Socket> ListenSocket::Accept() const {
    if (fd_ < 0) {
        SHARDORA_ERROR("accept on bad fd [%d]", fd_);
        return nullptr;
    }

    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int fd = accept(fd_, (sockaddr*)&addr, &addrlen);
    if (fd < 0) {
        if (errno != EAGAIN) {
            SHARDORA_ERROR("accept failed [%s]", strerror(errno));
        }

        return nullptr;
    }

    auto server_socket = std::make_shared<ServerSocket>(
            fd,
            addr.sin_addr.s_addr,
            ntohs(addr.sin_port),
            local_addr_,
            local_port_);
    if (server_socket == nullptr) {
        SHARDORA_ERROR("create tcp server socket failed");
        close(fd);
        return nullptr;
    }

    return server_socket;
}

}  // namespace tnet

}  // namespace shardora
