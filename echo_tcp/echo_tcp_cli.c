#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "echo_tcp.h"
#include "read_line.h"

int main(int argc, char **argv) {
    int cfd;
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *res_it; // iterator through getaddrinfo() result list

    char rcv_buf[MAX_RESP_LEN + 1];
    char snd_buf[MAX_REQ_LEN + 1];

    if (argc < 3 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "usage: %s server-host sending-string\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strlen(argv[2] + 1) > MAX_REQ_LEN) {
        fprintf(stderr, "echo string too big\n");
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

    // send message to server with newline character
    snprintf(snd_buf, MAX_REQ_LEN + 1, "%s\n", argv[2]);

    if (write(cfd, snd_buf, strlen(argv[2]) + 1) != strlen(argv[2]) + 1) {
        perror("write"); // partial/failed write
        exit(EXIT_FAILURE);
    }

    // read response

    switch(read_line(cfd, rcv_buf, MAX_RESP_LEN + 1)) {
        case -1:
            perror("read_line");
            exit(EXIT_FAILURE);
        case 0:
            fprintf(stderr, "Unexpected EOF from server");
            exit(EXIT_FAILURE);
        default:
            printf("%s", rcv_buf);
    }

    if(shutdown(cfd, SHUT_WR) == -1) {
        perror("shutdown");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

