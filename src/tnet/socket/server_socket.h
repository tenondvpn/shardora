#pragma once

#include "tnet/socket/tcp_socket.h"

namespace zjchain {

namespace tnet {

class ServerSocket : public TcpSocket {
public:
    in_addr_t peer_addr() const {
        return peer_addr_;
    }

    uint16_t peer_port() const {
        return peer_port_;
    }

    ServerSocket(
            int fd,
            in_addr_t peer_addr,
            uint16_t peer_port,
            in_addr_t local_addr,
            uint16_t local_port)
            : TcpSocket(local_addr, local_port),
              peer_addr_(peer_addr),
              peer_port_(peer_port) {
        SetFd(fd);
    }

    virtual ~ServerSocket() {}

private:
    in_addr_t peer_addr_;
    uint16_t peer_port_{ 0 };

private:
    DISALLOW_COPY_AND_ASSIGN(ServerSocket);
};

}  // namespace tnet

}  // namespace zjchain
