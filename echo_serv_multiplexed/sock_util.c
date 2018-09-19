#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

#include "sock_util.h"

int inet_connect(const char *host, const char *service, int type) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    hints.ai_family = AF_UNSPEC; // Allows IPv4 or IPv6
    hints.ai_socktype = type;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */;

    if (getaddrinfo(host, service, &hints, &result) != 0) {
        errno = ENOSYS;
        return -1;
    }

    // Walk through returned list until we find an address structure that can be used to successfully connect a socket
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue; // On error, try next address

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break; // Success

        // Connect failed: close this socket and try next address

        close(sfd);
    }

    freeaddrinfo(result);

    return (rp == NULL) ? -1 : sfd;
}

static int inet_passive_socket(const char *service, int type, socklen_t *addrlen, int doListen, int backlog) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    hints.ai_protocol = 0; // any protocol
    hints.ai_socktype = type;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, service, &hints, &result) != 0)
        return -1;

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;

        if (doListen) {
            int optval = 1;
            if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
                close(sfd);
                freeaddrinfo(result);
                return -1;
            }
        }

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sfd);
    }

    if (rp == NULL)
        return -1;

    if (doListen)
        if (listen(sfd, backlog) == -1) {
            freeaddrinfo(result);
            return -1;
        }

    if (addrlen != NULL)
        *addrlen = rp->ai_addrlen; // Return address structure size

    freeaddrinfo(result);
    return sfd;
}


int inet_listen(const char *service, int backlog, socklen_t *addrlen) {
    return inet_passive_socket(service, SOCK_STREAM, addrlen, 1, backlog);
}

int inet_bind(const char *service, int type, socklen_t *addrlen) {
    return inet_passive_socket(service, type, addrlen, 0, 0);
}

char *inet_address_str(const struct sockaddr *addr, socklen_t addrlen, char *addrStr, int addrStrLen) {
    char host[NI_MAXHOST], service[NI_MAXSERV];

    if (getnameinfo(addr, addrlen, host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV) == 0)
        snprintf(addrStr, addrStrLen, "(%s, %s)", host, service);
    else snprintf(addrStr, addrStrLen, "(?UNKNOWN?)");

    addrStr[addrStrLen - 1] = '\0'; // Ensure result is null-terminated
    return addrStr;
}

