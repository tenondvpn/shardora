#pragma once

#include "tnet/tnet_utils.h"
#include "tnet/socket/tcp_socket.h"

namespace zjchain {

namespace tnet {

class ClientSocket : public TcpSocket {
public:
    ClientSocket(
            in_addr_t peer_addr,
            uint16_t peer_port,
            in_addr_t local_addr,
            uint16_t local_port)
            : TcpSocket(local_addr, local_port),
              peer_addr_(peer_addr),
              peer_port_(peer_port) {}

    virtual ~ClientSocket() {}

    int Connect() const {
        if (fd_ < 0) {
            ZJC_ERROR("connect on bad fd [%d]", fd_);
            return -1;
        }
#ifndef _WIN32
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = peer_addr_;
        addr.sin_port = htons(peer_port_);
        int con_res = connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (con_res < 0) {
            if (errno == EINPROGRESS) {
                return 1;
            }

            ZJC_ERROR("connect failed on fd [%d] [%s] con_res[%d] errorno[%d]",
                    fd_, strerror(errno), con_res, errno);
            return -1;
        }
#endif
        return 0;
    }

    in_addr_t GetPeerAddr() const {
        return peer_addr_;
    }

    uint16_t GetPeerPort() const {
        return peer_port_;
    }

private:
    in_addr_t peer_addr_;
    uint16_t peer_port_{ 0 };

    DISALLOW_COPY_AND_ASSIGN(ClientSocket);
};

}  // namespace tnet

}  // namespace zjchain
