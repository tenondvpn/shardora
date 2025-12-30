#pragma once

#include "tnet/socket/tcp_socket.h"

namespace shardora {

namespace tnet {

class ServerSocket;

class ListenSocket : public TcpSocket {
public:
    bool Listen(int backlog) const;
    std::shared_ptr<Socket> Accept() const;

    ListenSocket(in_addr_t local_addr, uint16_t local_port)
            : TcpSocket(local_addr, local_port) {}

    virtual ~ListenSocket() {}

private:
    DISALLOW_COPY_AND_ASSIGN(ListenSocket);
};

}  // namespace tnet

}  // namespace shardora
