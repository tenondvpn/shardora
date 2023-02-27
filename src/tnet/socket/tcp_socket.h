#pragma once

#include "tnet/socket/socket.h"

namespace zjchain {

namespace tnet {

class TcpSocket : public Socket {
public:
    bool Bind() const {
        if (fd_ < 0) {
            ZJC_ERROR("bind on bad fd [%d]", fd_);
            return false;
        }
#ifndef _WIN32
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = local_addr_;
        addr.sin_port = htons(local_port_);
        int optval = 1;
        SetOption(SO_REUSEPORT, &optval, sizeof(optval));
        if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ZJC_ERROR("bind on fd [%d] failed [%s]", fd_, strerror(errno));
            return false;
        }
#endif
        return true;
    }

    in_addr_t GetLocalAddr() const {
        return local_addr_;
    }

    uint16_t GetLocalPort() const {
        return local_port_;
    }

protected:
    TcpSocket(in_addr_t local_addr, uint16_t local_port)
            : local_addr_(local_addr),
              local_port_(local_port) {}

    virtual ~TcpSocket() {}

    in_addr_t local_addr_;
    uint16_t local_port_{ 0 };

private:
    DISALLOW_COPY_AND_ASSIGN(TcpSocket);
};

}  // namespace tnet

}  // namespace zjchain
