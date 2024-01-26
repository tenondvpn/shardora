#pragma once

#include "tnet/tnet_utils.h"
#include "tnet/socket/client_socket.h"
#include "tnet/socket/listen_socket.h"
#include "tnet/socket/server_socket.h"

namespace zjchain {

namespace tnet {

class SocketFactory {
public:
    static ListenSocket* CreateTcpListenSocket(const std::string& spec) {
#ifndef _WIN32
        in_addr_t addr = 0;
        uint16_t port = 0;

        if (!ParseSpec(spec, &addr, &port)) {
            ZJC_ERROR("parse spec [%s] failed", spec.c_str());
            return NULL;
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            ZJC_ERROR("create socket failed [%s], spec [%s]",
                    strerror(errno), spec.c_str());
            return NULL;
        }

        ListenSocket* socket = new ListenSocket(addr, port);
        socket->SetFd(fd);

        if (!socket->Bind()) {
            ZJC_ERROR("bind failed, spec [%s]", spec.c_str());
            socket->Free();
            return NULL;
        }

        return socket;
#endif
        return NULL;
    }

    static ClientSocket* CreateTcpClientSocket(
            const std::string& peer,
            const std::string& local) {
#ifndef _WIN32
        in_addr_t peer_addr = 0;
        uint16_t peer_port = 0;
        in_addr_t local_addr = 0;
        uint16_t local_port = 0;
        if (!ParseSpec(peer, &peer_addr, &peer_port)) {
            ZJC_ERROR("parse spec [%s] failed", peer.c_str());
            return NULL;
        }

        if (!local.empty() && !ParseSpec(local, &local_addr, &local_port)) {
            ZJC_ERROR("parse spec [%s] failed", local.c_str());
            return NULL;
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            ZJC_ERROR("create socket failed [%s]", strerror(errno));
            return NULL;
        }

        ClientSocket* socket = new ClientSocket(
                peer_addr,
                peer_port,
                local_addr,
                local_port);
        socket->SetFd(fd);

        if (!local.empty() && !socket->Bind()) {
            ZJC_ERROR("bind failed, spec [%s]", local.c_str());
            socket->Free();
            return NULL;
        }

        return socket;
#endif
        return NULL;
    }

    static ServerSocket* CreateTcpServerSocket(
            int fd,
            in_addr_t peer_addr,
            uint16_t peer_port,
            in_addr_t local_addr,
            uint16_t local_port) {
        return new ServerSocket(fd, peer_addr, peer_port, local_addr, local_port);
    }
};

}  // namespace tnet

}  // namespace zjchain
