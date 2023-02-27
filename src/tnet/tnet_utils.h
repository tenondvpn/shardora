#pragma once

#ifndef _WIN32
#include <arpa/inet.h>
#include<netdb.h>
#endif
#include <errno.h>

#include <functional>

#include "common/utils.h"
#include "common/split.h"
#include "common/log.h"

namespace zjchain {

namespace tnet {

enum TnetErrorCode {
    kTnetSuccess = 0,
    kTnetError = 1,
    kTnetTimeout = 2,
};

static const int kEpollMaxWaitTime = 100;
static const uint32_t kEpollMaxEvents = 256;
static const uint32_t kDvMaxEvents = 10240u;

class TcpConnection;
class Packet;

typedef std::function<bool(TcpConnection&)> ConnectionHandler;
typedef std::function<bool(TcpConnection*, Packet&)> PacketHandler;
typedef std::function<void()> WriteableHandler;

inline static bool ParseSpec(const std::string& s, in_addr_t* addr, uint16_t* port) {
#ifndef _WIN32
    common::Split<> split(s.c_str(), ':', s.size());
    if (split.Count() != 2) {
        ZJC_ERROR("bad spec [%s]", s.c_str());
        return false;
    }

    const std::string& str_host = split[0];
    const std::string& str_port = split[1];
    if (str_host != "*") {
        addrinfo hint;
        addrinfo* result;
        memset(&hint, 0, sizeof(hint));
        hint.ai_family = AF_INET;
        if (getaddrinfo(str_host.c_str(), NULL, &hint, &result) != 0) {
            ZJC_ERROR("getaddrinfo failed");
            return false;
        }

        *addr = (uint32_t)((sockaddr_in *)(result->ai_addr))->sin_addr.s_addr;
        freeaddrinfo(result);
    } else {
        *addr = INADDR_ANY;
    }

    int tmp_port = atoi(str_port.c_str());
    if (tmp_port > UINT16_MAX || tmp_port < 0) {
        ZJC_ERROR("bad port number [%d]", tmp_port);
        return false;
    }

    *port = tmp_port;
#endif
    return true;
}

}  // namespace tnet

}  // namespace zjchain
