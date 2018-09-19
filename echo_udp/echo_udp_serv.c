#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "echo_udp.h"

#define ADDRSTRLEN (NI_MAXHOST + NI_MAXSERV + 10)

int main(int argc, char **argv) {
    int serv_fd;

    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    hints.ai_addr = NULL;
    hints.ai_socktype = SOCK_DGRAM;
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
        serv_fd = socket(res_it->ai_family, res_it->ai_socktype, res_it->ai_protocol);
        if (serv_fd == -1)
            continue; // upon failure try next address

        if (bind(serv_fd, res_it->ai_addr, res_it->ai_addrlen) == 0)
            break; // successfully binded, exit loop

        // bind failed, close socket and try next address
        close(serv_fd);
    }

    // free getaddrinfo() result list
    freeaddrinfo(res_it);

    if (res_it == NULL) {
        perror("couldn't bind socket to any address");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        // to store client's address
        struct sockaddr_storage cli_addr;
        socklen_t addrlen = sizeof(cli_addr);

        // buffers for client's address string
        char host[NI_MAXHOST];
        char service[NI_MAXSERV];
        char addr_str[ADDRSTRLEN];

        // buffers for incoming and outgoing messages
        char rcv_buf[REQ_BUF_SIZE + 1];
        char snd_buf[RESP_BUF_SIZE + 1]; // +1 for terminating byte

        // length of received and sent messages
        ssize_t num_read;
        size_t num_send;

        // receive message and save client's address
        num_read = recvfrom(serv_fd, rcv_buf, REQ_BUF_SIZE, 0, (struct sockaddr *) &cli_addr, &addrlen);
        if(num_read == -1) {
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }
        rcv_buf[num_read] = '\0';

        // get client's host and service name and print it
        if (getnameinfo((struct sockaddr *) &cli_addr, addrlen, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0)
            snprintf(addr_str, ADDRSTRLEN, "%s, %s", host, service);
        else
            snprintf(addr_str, ADDRSTRLEN, "(UNKNOWN)");
        printf("Echo request from %s\n", addr_str);

        // form response string
        num_send = num_read + strlen(APPENDIX);
        snprintf(snd_buf, RESP_BUF_SIZE + 1, "%s%s", rcv_buf, APPENDIX);

        // and send it
        if (sendto(serv_fd, snd_buf, num_send, 0, (struct sockaddr *) &cli_addr, addrlen) != num_send) {
            perror("sendto");
            fprintf(stderr, "Error echoing response to %s\n", addr_str);
        }
    }
}
