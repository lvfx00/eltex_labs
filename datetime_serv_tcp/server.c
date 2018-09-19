#include <signal.h>
#include <syslog.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "datetime_serv_tcp.h"

#define BACKLOG 10

// SIGCHLD handler to reap dead child processes
static void reaper(int sig) {
    int saved_errno = errno; // Save errno in case changed here

    while (waitpid(-1, NULL, WNOHANG) > 0)
        continue;

    errno = saved_errno;
}

static void handle_request(int cfd) {
    char rcv_buf[BUF_SIZE + 1];
    char snd_buf[BUF_SIZE + 1];
    ssize_t num_read;

    num_read = read(cfd, rcv_buf, BUF_SIZE);
    if (num_read == -1) {
        perror("read");
        exit(EXIT_FAILURE);
    }
    rcv_buf[num_read] = '\0';

    // valid request, process it
    if (strcmp(rcv_buf, REQUEST_STR) == 0) {
        // get current datetime and write it to snd_buf
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);

        snprintf(snd_buf, BUF_SIZE + 1, "now: %d-%d-%d %d:%d:%d\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

        if(write(cfd, snd_buf, strlen(snd_buf)) != strlen(snd_buf)) {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[]) {
    int lfd, cfd;
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = reaper;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Error from sigaction(): %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC; // Allows IPv4 or IPv6
    hints.ai_flags = AI_PASSIVE; // Use wildcard IP address

    if (getaddrinfo(NULL, SERVICE, &hints, &result) != 0)
        return -1;

    struct addrinfo *rp;

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        lfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (lfd == -1)
            continue;

        int optval = 1;
        if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            close(lfd);
            freeaddrinfo(result);
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        if (bind(lfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // Success

        // bind() failed: close this socket and try next address
        close(lfd);
    }

    freeaddrinfo(result);

    if (rp == NULL) {
        fprintf(stderr, "Couldn't bind socket to any address\n");
        exit(EXIT_FAILURE);
    }

    if (listen(lfd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        cfd = accept(lfd, NULL, NULL); // Wait for connection
        if (cfd == -1) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        // Handle each client request in a new child process
        switch (fork()) {
            case -1:
                perror("fork");
                close(cfd); // Give up on this client
                break;

            case 0: // Child
                close(lfd); // close unneeded copy of listening socket
                handle_request(cfd);
                exit(EXIT_SUCCESS);

            default: // Parent
                close(cfd); // close unneeded copy of connected socket
                break; // loop to accept next connection
        }
    }
}
