#include "common/utils.h"

#include <iostream>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

namespace zjchain {

namespace common {

#ifndef MAKE_CLIENT_LIB
int RemoteReachable(const std::string& ip, uint16_t port, bool* reachable) {
    *reachable = false;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *host;
    if ((host = gethostbyname(ip.c_str())) == NULL) {
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    memcpy(&addr.sin_addr, host->h_addr_list[0], host->h_length);
    addr.sin_family = host->h_addrtype;
    addr.sin_port = htons(port);
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
#ifdef HAVE_SOLARIS
            if (errno == ECONNREFUSED) {
                close(sockfd);
                return(1);
            } else {
#endif    
                return -1;
#ifdef HAVE_SOLARIS
            }
#endif    
        }

        fd_set fdrset, fdwset;
        FD_ZERO(&fdrset);
        FD_SET(sockfd, &fdrset);
        fdwset = fdrset;
        struct timeval timeout;
        long timeout_sec = 2, timeout_usec = 0;
        timeout.tv_sec = timeout_sec + timeout_usec / 1000000;
        timeout.tv_usec = timeout_usec % 1000000;
        if (select(sockfd+1, &fdrset, &fdwset, NULL, timeout.tv_sec + timeout.tv_usec > 0 ? &timeout : NULL) == 0) {
            close(sockfd);
            return -1;
        }

        if (FD_ISSET(sockfd, &fdrset) || FD_ISSET(sockfd, &fdwset)) {
            int error = 0;
            socklen_t errlen = sizeof(error);
            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &errlen) != 0) {
                close(sockfd);
                return -1;
            }

            if (error != 0) {
                close(sockfd);
                return -1;
            }
        } else {
            return -1;
        }
    }

     *reachable = true;
    close(sockfd);
return 0;
    std::string cmd = std::string("tcping ") + ip + std::string(" ") + std::to_string(port) + " -t 3";
    FILE *pf = NULL;
    if ((pf = popen(cmd.c_str(), "r")) == NULL) {
        return -1;
    }

    std::string res;
    char buf[1024] = { 0 };
    while (fgets(buf, sizeof buf, pf)) {
        res += buf;
    }

    pclose(pf);
    if (res.find(std::string("open")) != res.npos && res.find(ip) != res.npos) {
        *reachable = true;
    }

    return 0;
}
#else

int RemoteReachable(const std::string& ip, uint16_t port, bool* reachable) {

    return -1;
}

#endif

}  // namespace common

}  // namespace zjchain
