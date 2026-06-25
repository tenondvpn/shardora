#pragma once

#include <unistd.h>

#include "tnet/tnet_utils.h"
#include "tnet/socket/client_socket.h"
#include "tnet/socket/listen_socket.h"
#include "tnet/socket/server_socket.h"

namespace shardora {

namespace tnet {

class SocketFactory {
public:
    static ListenSocket* CreateTcpListenSocket(const std::string& spec) {
#ifndef _WIN32
        in_addr_t addr = 0;
        uint16_t port = 0;

        if (!ParseSpec(spec, &addr, &port)) {
            SHARDORA_ERROR("parse spec [%s] failed", spec.c_str());
            return NULL;
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            SHARDORA_ERROR("create socket failed [%s], spec [%s]",
                    strerror(errno), spec.c_str());
            return NULL;
        }

        ListenSocket* socket = new ListenSocket(addr, port);
        socket->SetFd(fd);
        if (!socket->Bind()) {
            SHARDORA_ERROR("bind failed, spec [%s]", spec.c_str());
            // Bug fix #19: On bind failure, the old code called socket->Free()
            // (which is a no-op) then created a new ListenSocket with the SAME fd.
            // This meant the fd was shared between the freed and new socket objects.
            // Now we properly close the fd and create a fresh one for retry.
            delete socket;
            close(fd);
            
            fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                SHARDORA_ERROR("create retry socket failed [%s]", strerror(errno));
                return NULL;
            }
            socket = new ListenSocket(addr, 0);
            socket->SetFd(fd);
            if (!socket->Bind()) {
                SHARDORA_ERROR("retry bind also failed, spec [%s]", spec.c_str());
                delete socket;
                close(fd);
                return NULL;
            }
        }

        return socket;
#endif
        return NULL;
    }

    static std::shared_ptr<Socket> CreateTcpClientSocket(
            const std::string& peer,
            const std::string& local) {
#ifndef _WIN32
        in_addr_t peer_addr = 0;
        uint16_t peer_port = 0;
        in_addr_t local_addr = 0;
        uint16_t local_port = 0;
        if (!ParseSpec(peer, &peer_addr, &peer_port)) {
            SHARDORA_ERROR("parse spec [%s] failed", peer.c_str());
            return NULL;
        }

        if (!local.empty() && !ParseSpec(local, &local_addr, &local_port)) {
            SHARDORA_ERROR("parse spec [%s] failed", local.c_str());
            return NULL;
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            SHARDORA_ERROR("create socket failed [%s]", strerror(errno));
            return NULL;
        }

        auto client_socket = std::make_shared<ClientSocket>(
                peer_addr,
                peer_port,
                local_addr,
                local_port);
        client_socket->SetFd(fd);

        if (!local.empty() && !client_socket->Bind()) {
            SHARDORA_ERROR("bind failed, spec [%s]", local.c_str());
            client_socket->Free();
            return NULL;
        }

        return client_socket;
#endif
        return NULL;
    }

    static std::shared_ptr<Socket> CreateTcpServerSocket(
            int fd,
            in_addr_t peer_addr,
            uint16_t peer_port,
            in_addr_t local_addr,
            uint16_t local_port) {
        return std::make_shared<ServerSocket>(fd, peer_addr, peer_port, local_addr, local_port);
    }
};

}  // namespace tnet

}  // namespace shardora
