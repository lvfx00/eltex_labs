#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "datetime_serv_tcp.h"

int main(int argc, char **argv) {
    int cfd;
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *res_it; // iterator through getaddrinfo() result list

    char rcv_buf[BUF_SIZE + 1];

    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "usage: %s server-host\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // obtain list of addresses that we can connect to
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    hints.ai_family = AF_UNSPEC; // Allow IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    if (getaddrinfo(argv[1], SERVICE, &hints, &result) != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    // try to create and connect socket to one of addresses from result list
    for (res_it = result; res_it != NULL; res_it = res_it->ai_next) {
        cfd = socket(res_it->ai_family, res_it->ai_socktype, res_it->ai_protocol);
        if (cfd == -1)
            continue;

        if (connect(cfd, res_it->ai_addr, res_it->ai_addrlen) != -1)
            break; // success

        // connect failed, close socket and try another address
        close(cfd);
    }

    if (res_it == NULL) {
        fprintf(stderr, "Could not connect socket to any address");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(result);

    if (write(cfd, REQUEST_STR, strlen(REQUEST_STR)) != strlen(REQUEST_STR)) {
        perror("write"); // partial/failed write
        exit(EXIT_FAILURE);
    }

    // read response
    ssize_t num_read = read(cfd, rcv_buf, BUF_SIZE);
    if(num_read == -1) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    rcv_buf[num_read] = '\0';
    printf("%s", rcv_buf);

    if(close(cfd) == -1) {
        perror("close");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}



