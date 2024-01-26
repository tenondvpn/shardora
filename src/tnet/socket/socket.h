#pragma once

#include <stdint.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
#include <errno.h>

#include "common/utils.h"
#include "tnet/tnet_utils.h"

namespace zjchain {

namespace tnet {

class Socket {
public:
    int GetFd() const { return fd_; }
    void SetFd(int32_t fd) {
        fd_ = fd;
    }

    int Read(void* buf, size_t len) const;
    int Write(const void* buf, size_t len) const;
    void Close();
    void ShutdownWrite();
    void ShutdownRead();
    bool SetNonBlocking(bool enable) const;
    bool SetCloseExec(bool enable) const;
    bool SetTcpNoDelay(bool enable) const;
    bool SetSoLinger(bool enable, int seconds) const;
    bool SetTcpKeepAlive(int idleTime, int keepInterval, int cnt) const;
    bool SetSoRcvBuf(int buffSize) const;
    bool SetSoSndBuf(int buffSize) const;
    bool SetOption(int option, const void* value, size_t len) const;
    bool GetSoError(int* code) const;
    virtual void Free();
    int GetIpPort(std::string* ip, uint16_t* port);

protected:
    Socket();
    virtual ~Socket();

    int32_t fd_{ -1 };

private:

    DISALLOW_COPY_AND_ASSIGN(Socket);
};

}  // namespace tnet

}  // namespace zjchain
