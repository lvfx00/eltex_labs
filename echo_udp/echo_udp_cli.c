#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "echo_udp.h"

int main(int argc, char **argv) {
    int cfd;

    if (argc < 3 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "usage: %s server-host sending-string\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints;
    struct addrinfo *result;

    // obtain list of addresses that we can connect to
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    hints.ai_family = AF_UNSPEC; // Allow IPv4 and IPv6
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    if (getaddrinfo(argv[1], SERVICE, &hints, &result) != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    // try to create and connect socket to one of addresses from result list
    struct addrinfo *res_it; // iterator through getaddrinfo() result list

    for (res_it = result; res_it != NULL; res_it = res_it->ai_next) {
        cfd = socket(res_it->ai_family, res_it->ai_socktype, res_it->ai_protocol);
        if (cfd == -1)
            continue;

        if (connect(cfd, res_it->ai_addr, res_it->ai_addrlen) != -1)
            break; // success

        // connect failed, close socket and try another address
        close(cfd);
    }

    freeaddrinfo(result);

    if (res_it == NULL) {
        fprintf(stderr, "Could not connect socket to any address\n");
        exit(EXIT_FAILURE);
    }

    // send echop request to server
    if (send(cfd, argv[2], strlen(argv[2]), 0) != strlen(argv[2])) {
        perror("send"); // partial/failed write
        exit(EXIT_FAILURE);
    }

    // read response
    ssize_t num_read;
    char rcv_buf[RESP_BUF_SIZE + 1];

    num_read = recv(cfd, rcv_buf, RESP_BUF_SIZE, 0);
    if(num_read == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }
    rcv_buf[num_read] = '\0'; // unsure that recieved string is null-terminated
    printf("%s\n", rcv_buf);

    if(close(cfd) == -1) {
        perror("close");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}

