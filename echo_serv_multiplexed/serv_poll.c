#define  _GNU_SOURCE // for POLLRDHUP

#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

#include "echo_serv_multiplexed.h"
#include "sock_util.h"
#include "serv_util.h"

int main() {
    int serv_tcp_sockfd;
    int serv_udp_sockfd;
    // to store the length of server's sockets addresses
    socklen_t addrlen_tcp;
    socklen_t addrlen_udp;

    int udp_buf_empty = 1;
    char udp_rcvbuf[BUF_SIZE];
    ssize_t udp_num_recv;
    struct sockaddr_storage udp_cli_addr;
    socklen_t udp_cli_addrlen;

    unsigned tcp_clients_num = 0;
    struct client_info infos[MAX_TCP_CONN_NUM];

    for (int i = 0; i < MAX_TCP_CONN_NUM; ++i)
        infos[i].sock_fd = EMPTY_INFO;

    struct pollfd poll_fds[MAX_TCP_CONN_NUM + 2]; // +2 for serv_sockfds
    int ready;

    // create listening TCP socket
    serv_tcp_sockfd = inet_listen(SERVICE, BACKLOG, &addrlen_tcp);
    if (serv_tcp_sockfd == -1) {
        perror("inet_listen");
        exit(EXIT_FAILURE);
    }

    // set TCP socket in non-blocking mode
    int flags = fcntl(serv_tcp_sockfd, F_GETFL);
    if (flags == -1) {
        perror("fcntl - F_GETFL");
        exit(EXIT_FAILURE);
    }
    flags |= O_NONBLOCK;
    if (fcntl(serv_tcp_sockfd, F_SETFL, flags) == -1) {
        perror("fcntl - F_SETFL");
        exit(EXIT_FAILURE);
    }

    // create UDP socket for message receiving
    serv_udp_sockfd = inet_bind(SERVICE, SOCK_DGRAM, &addrlen_udp);
    if (serv_udp_sockfd == -1) {
        perror("inet_bind");
        exit(EXIT_FAILURE);
    }

    poll_fds[0].fd = serv_tcp_sockfd;
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = serv_udp_sockfd;
    poll_fds[1].events = POLLIN | POLLOUT;

    for (;;) {
        int new_clients_num = 0;

        ready = poll(poll_fds, 2 + tcp_clients_num, -1);
        if (ready == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        // check whether we have established incoming connection on TCP listening socket
        if (tcp_clients_num < MAX_TCP_CONN_NUM && poll_fds[0].revents & POLLIN) {
            int cfd;
            struct sockaddr_storage cli_addr;
            socklen_t cli_addrlen = sizeof(cli_addr);

            cfd = accept(serv_tcp_sockfd, (struct sockaddr *) &cli_addr, &cli_addrlen);
            if (cfd == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
            } else {
                char addrbuf[IS_ADDR_STR_LEN];
                inet_address_str((struct sockaddr *) &cli_addr, cli_addrlen, addrbuf, IS_ADDR_STR_LEN);
                printf("Got TCP connection from %s\n", addrbuf);

                // save connection info
                struct client_info *info = find_empty(infos, MAX_TCP_CONN_NUM);
                if (!info) {
                    perror("find_empty"); // there must be an empty info structure
                    exit(EXIT_FAILURE);
                }
                info->sock_fd = cfd;
                info->cli_addr = cli_addr;
                info->cli_addrlen = cli_addrlen;
                info->available = BUF_SIZE;

                // add to pollfd array
                poll_fds[2 + tcp_clients_num].fd = cfd;
                poll_fds[2 + tcp_clients_num].events = POLLIN | POLLOUT;

                // increase number of connected tcp clients
                ++new_clients_num;
            }
        }

        // check whether we have incoming message on UDP socket
        if (poll_fds[1].revents & POLLIN && udp_buf_empty) {
            udp_cli_addrlen = sizeof(udp_cli_addr);
            // read incoming data
            udp_num_recv = recvfrom(serv_udp_sockfd, udp_rcvbuf, BUF_SIZE, 0, (struct sockaddr *) &udp_cli_addr, &udp_cli_addrlen);
            if(udp_num_recv == -1) {
                perror("recvfrom");
                exit(EXIT_FAILURE);
            }

            udp_buf_empty = 0;

            char addrbuf[IS_ADDR_STR_LEN];
            inet_address_str((struct sockaddr *) &udp_cli_addr, udp_cli_addrlen, addrbuf, IS_ADDR_STR_LEN);
            printf("Got UDP query from %s\n", addrbuf);
        }
        // check whether we have not echoed on UDP socket
        if (poll_fds[1].revents & POLLOUT && !udp_buf_empty) {
            if (sendto(serv_udp_sockfd, udp_rcvbuf, udp_num_recv, 0, (struct sockaddr *) &udp_cli_addr, udp_cli_addrlen) != udp_num_recv) {
                perror("sendto");
                exit(EXIT_FAILURE);
            }

            udp_buf_empty = 1;
        }

        // check whether i/o available on one of the TCP clients
        for (int i = 2; i < tcp_clients_num + 2; ++i) {
            ssize_t num_recv;
            ssize_t num_send;
            struct client_info *cli = find_by_fd(poll_fds[i].fd, infos, MAX_TCP_CONN_NUM);

            // input available
            if (poll_fds[i].revents & POLLIN && cli->available > 0) {
                num_recv = recv(cli->sock_fd, cli->rcvbuf + (BUF_SIZE - cli->available), cli->available, 0);
                if (num_recv == -1) {
                    perror("recv");
                    exit(EXIT_FAILURE);
                }
                
                if (num_recv == 0) { // client disconnected from the server
                    char addrbuf[IS_ADDR_STR_LEN];
                    inet_address_str((struct sockaddr *) &(cli->cli_addr), cli->cli_addrlen, addrbuf, IS_ADDR_STR_LEN);
                    printf("Lost TCP connection from %s\n", addrbuf);

                    if(close(cli->sock_fd) == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }

                    // remove data about client and pollfd structure
                    if(remove_from_pollfds(cli->sock_fd, poll_fds, 2 + tcp_clients_num) == -1) {
                        perror("remove_from_pollfds");
                        exit(EXIT_FAILURE);
                    }

                    cli->sock_fd = EMPTY_INFO;

                    // decrease number of connected tcp clients
                    --tcp_clients_num;
                }

                cli->available -= num_recv;
            }

            // output available
            if (poll_fds[i].revents & POLLOUT && cli->available < BUF_SIZE) {
                num_send = send(cli->sock_fd, cli->rcvbuf, BUF_SIZE - cli->available, 0);
                if (num_send == -1) {
                    perror("send");
                    exit(EXIT_FAILURE);
                }

                // TODO More efficent implementation is to use circular buffer instead of memmove()
                // shift remaining input in the beginning of the buffer
                memmove(cli->rcvbuf, cli->rcvbuf + num_send, BUF_SIZE - num_send - cli->available);
                cli->available += num_send;
            }

            tcp_clients_num += new_clients_num;
        }
    }
}


