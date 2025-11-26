#pragma once

#include "tnet/socket/tcp_socket.h"

namespace shardora {

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
        SHARDORA_DEBUG("memory check create server socket: %p", this);
        SetFd(fd);
    }

    virtual ~ServerSocket() {
        SHARDORA_DEBUG("memory check release server socket: %p", this);
    }

private:
    in_addr_t peer_addr_;
    uint16_t peer_port_{ 0 };

private:
    DISALLOW_COPY_AND_ASSIGN(ServerSocket);
};

}  // namespace tnet

}  // namespace shardora
