#define  _GNU_SOURCE // for POLLRDHUP

#include <stdio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "echo_serv_multiplexed.h"
#include "sock_util.h"
#include "serv_util.h"

int main() {
    int max_fd_num = 0; // for select()
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

    // create listening TCP socket
    serv_tcp_sockfd = inet_listen(SERVICE, BACKLOG, &addrlen_tcp);
    if (serv_tcp_sockfd == -1) {
        perror("inet_listen");
        exit(EXIT_FAILURE);
    }

    // update max_fd_num
    max_fd_num = (max_fd_num < serv_tcp_sockfd) ? serv_tcp_sockfd : max_fd_num;

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

    // update max_fd_num
    max_fd_num = (max_fd_num < serv_udp_sockfd) ? serv_udp_sockfd : max_fd_num;

    fd_set readfds, writefds; // fd_sets for select
    int ready;
    int count;
    ssize_t num_recv;
    ssize_t num_send;

    for (;;) {
        // initialize fd_set structures
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(serv_tcp_sockfd, &readfds);
        FD_SET(serv_udp_sockfd, &readfds);
        FD_SET(serv_udp_sockfd, &writefds);

        count = tcp_clients_num;
        for (int i = 0; i < MAX_TCP_CONN_NUM; ++i) {
            if (count == 0)
                break;

            // find connected clients info structures
            if (infos[i].sock_fd != EMPTY_INFO) {
                FD_SET(infos[i].sock_fd, &readfds);
                FD_SET(infos[i].sock_fd, &writefds);
                --count;
            }
        }
        if (count != 0) {
            fprintf(stderr, "Unable to find all TCP-clients' socket fds\n");
            exit(EXIT_FAILURE);
        }

        ready = select(max_fd_num + 1, &readfds, &writefds, NULL, NULL);
        if (ready == -1) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        int new_tcp_clients_num = tcp_clients_num;

        count = tcp_clients_num;
        for (int i = 0; i < MAX_TCP_CONN_NUM; ++i) {
            if (count == 0)
                break;

            // find connected clients info structures
            if (infos[i].sock_fd != EMPTY_INFO) {
                struct client_info *cli = &infos[i];

                // input available
                if (FD_ISSET(cli->sock_fd, &readfds) && cli->available > 0) {
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

                        cli->sock_fd = EMPTY_INFO;
                        // decrease number of connected tcp clients
                        --new_tcp_clients_num;
                    }

                    cli->available -= num_recv;
                }

                // output available
                if (FD_ISSET(cli->sock_fd, &writefds) && cli->available < BUF_SIZE) {
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

                --count;
            }
        }
        if (count != 0) {
            fprintf(stderr, "Unable to find all TCP-clients' socket fds\n");
            exit(EXIT_FAILURE);
        }

        tcp_clients_num = new_tcp_clients_num;

        // check whether we have established incoming connection on TCP listening socket
        if (tcp_clients_num < MAX_TCP_CONN_NUM && FD_ISSET(serv_tcp_sockfd, &readfds)) {
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

                // update max_fd_num
                max_fd_num = (max_fd_num < cfd) ? cfd : max_fd_num;

                // increase number of connected tcp clients
                ++tcp_clients_num;
            }
        }

        // check whether we have incomming message on UDP socket
        if (FD_ISSET(serv_udp_sockfd, &readfds) && udp_buf_empty) {
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
        if(FD_ISSET(serv_udp_sockfd, &writefds) && !udp_buf_empty) {
            if (sendto(serv_udp_sockfd, udp_rcvbuf, udp_num_recv, 0, (struct sockaddr *) &udp_cli_addr, udp_cli_addrlen) != udp_num_recv) {
                perror("sendto");
                exit(EXIT_FAILURE);
            }

            udp_buf_empty = 1;
        }
    }
}