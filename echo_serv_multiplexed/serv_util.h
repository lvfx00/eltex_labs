#pragma once

#include <stdlib.h>
#include <sys/socket.h>
#include <poll.h>
#include "echo_serv_multiplexed.h"

#define EMPTY_INFO -1

struct client_info {
    int sock_fd;
    struct sockaddr_storage cli_addr;
    socklen_t cli_addrlen; // = sizeof(cli_addr);
    char rcvbuf[BUF_SIZE];
    size_t available;
};

struct client_info *find_by_fd(int cfd, struct client_info *infos, size_t len);

struct client_info *find_empty(struct client_info *infos, size_t len);

int remove_from_pollfds(int cfd, struct pollfd *poll_fds, int fdnum);

