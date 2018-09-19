#define  _GNU_SOURCE // for POLLRDHUP

#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>

#include "echo_serv_multiplexed.h"
#include "sock_util.h"
#include "serv_util.h"

#define MAX_EVENTS 10

int main() {
    int serv_tcp_sockfd;
    int serv_udp_sockfd;
    int epoll_fd;
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

    epoll_fd = epoll_create(2 + MAX_TCP_CONN_NUM);
    if (epoll_fd == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    // add listening TCP socket
    struct epoll_event tcp_ev;
    tcp_ev.data.fd = serv_tcp_sockfd;
    tcp_ev.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, serv_tcp_sockfd, &tcp_ev);

    // add server's UDP socket
    struct epoll_event udp_ev;
    udp_ev.data.fd = serv_udp_sockfd;
    udp_ev.events = EPOLLIN | EPOLLOUT;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, serv_udp_sockfd, &udp_ev);

    struct epoll_event events[MAX_EVENTS]; // event list for epoll_wait results
    int ready;
    for (;;) {
        ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (ready == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }
        // process events
        for(int i = 0; i < ready; ++i) {
            // we have established incoming connection on TCP listening socket
            if (events[i].data.fd == serv_tcp_sockfd && tcp_clients_num < MAX_TCP_CONN_NUM) {
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

                    // add to epoll interests
                    struct epoll_event ev;
                    ev.data.fd = cfd;
                    ev.events = EPOLLIN | EPOLLOUT;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cfd, &ev);

                    // increase number of connected tcp clients
                    ++tcp_clients_num;
                }
            }
            // incoming message on UDP socket
            else if (events[i].data.fd == serv_udp_sockfd) {
                if (events[i].events & EPOLLIN && udp_buf_empty) {
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
                if (events[i].events & EPOLLOUT && !udp_buf_empty) {
                    if (sendto(serv_udp_sockfd, udp_rcvbuf, udp_num_recv, 0, (struct sockaddr *) &udp_cli_addr, udp_cli_addrlen) != udp_num_recv) {
                        perror("sendto");
                        exit(EXIT_FAILURE);
                    }

                    udp_buf_empty = 1;
                }
            }
            // i/o available on one of the TCP clients
            else {
                ssize_t num_recv;
                ssize_t num_send;
                struct client_info *cli = find_by_fd(events[i].data.fd, infos, MAX_TCP_CONN_NUM);

                // input available
                if (events[i].events & EPOLLIN && cli->available > 0) {
                    num_recv = recv(cli->sock_fd, cli->rcvbuf + (BUF_SIZE - cli->available), cli->available, 0);
                    if (num_recv == -1) {
                        perror("recv");
                        exit(EXIT_FAILURE);
                    }
                    cli->available -= num_recv;
                    
                    // client disconnected from the server
                    if (num_recv == 0) { 
                        char addrbuf[IS_ADDR_STR_LEN];
                        inet_address_str((struct sockaddr *) &(cli->cli_addr), cli->cli_addrlen, addrbuf, IS_ADDR_STR_LEN);
                        printf("Lost TCP connection from %s\n", addrbuf);

                        // automatically removes from epoll einterests
                        if(close(cli->sock_fd) == -1) {
                            perror("close");
                            exit(EXIT_FAILURE);
                        }

                        cli->sock_fd = EMPTY_INFO;

                        // decrease number of connected tcp clients
                        --tcp_clients_num;
                    }
                }

                // output available
                if (events[i].events & EPOLLOUT && cli->available < BUF_SIZE) {
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
            }
        }
    }
}
