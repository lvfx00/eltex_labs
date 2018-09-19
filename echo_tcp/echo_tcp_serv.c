#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "echo_tcp.h"
#include "read_line.h"

#define BACKLOG 10
#define ADDRSTRLEN (NI_MAXHOST + NI_MAXSERV + 10)

int main(int argc, char **argv) {
    int lfd;

    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    hints.ai_addr = NULL;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV; // Use wildcard IP address

    // get list of addresses that uses PORT_NUM
    if (getaddrinfo(NULL, SERVICE, &hints, &result) != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    // try to create socket and bind it to one of addresses in resulting list
    struct addrinfo *res_it; // iterator through getaddrinfo() result list

    for (res_it = result; res_it != NULL; res_it = res_it->ai_next) {
        lfd = socket(res_it->ai_family, res_it->ai_socktype, res_it->ai_protocol);
        if (lfd == -1)
            continue; // upon failure try next address

        int optval = 1;
        if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            close(lfd);
            freeaddrinfo(result);
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        if (bind(lfd, res_it->ai_addr, res_it->ai_addrlen) == 0)
            break; // successfully binded, exit loop

        // bind failure, close socket and try next address
        close(lfd);
    }

    // free getaddrinfo() result list
    freeaddrinfo(res_it);

    if (res_it == NULL) {
        perror("couldn't bind socket to any address");
        exit(EXIT_FAILURE);
    }

    if (listen(lfd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        int cfd; // file descriptor of client's socket
        struct sockaddr_storage cli_addr;
        socklen_t addrlen = sizeof(cli_addr);

        // for client's address string
        char host[NI_MAXHOST];
        char service[NI_MAXSERV];
        char addr_str[ADDRSTRLEN];

        cfd = accept(lfd, (struct sockaddr *) &cli_addr, &addrlen);
        if (cfd == -1) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        // get client's host and service name and print it
        if (getnameinfo((struct sockaddr *) &cli_addr, addrlen, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0)
            snprintf(addr_str, ADDRSTRLEN, "%s, %s", host, service);
        else
            snprintf(addr_str, ADDRSTRLEN, "(UNKNOWN)");
        printf("Connection from %s\n", addr_str);

        char rcv_buf[MAX_REQ_LEN + 1];
        char snd_buf[MAX_RESP_LEN + 1];

        if (read_line(cfd, rcv_buf, MAX_REQ_LEN + 1) <= 0) {
            close(cfd);
            continue; // failed read, skip request
        }

        // remove newline character from incoming string
        size_t len = strlen(rcv_buf);
        if (rcv_buf[len - 1] == '\n')
            rcv_buf[len - 1] = '\0';

        // form string to send
        snprintf(snd_buf, MAX_RESP_LEN + 1, "%s%s\n", rcv_buf, APPENDIX);
        // and send it
        if (write(cfd, snd_buf, strlen(snd_buf)) != strlen(snd_buf))
            fprintf(stderr, "Error on write");

        if (close(cfd) == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
    }
}