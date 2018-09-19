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

#include "datetime_serv_udp.h"

// SIGCHLD handler to reap dead child processes
static void reaper(int sig) {
    int saved_errno = errno; // Save errno in case changed here

    while (waitpid(-1, NULL, WNOHANG) > 0)
        continue;

    errno = saved_errno;
}

static void handle_request(int sfd, char *rcv_buf, const struct sockaddr_storage *cli_addr, socklen_t addrlen) {
    char snd_buf[BUF_SIZE + 1];

    // if request valid, process it
    if (strcmp(rcv_buf, REQUEST_STR) == 0) {
        // get current datetime and write it to snd_buf
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);

        snprintf(snd_buf, BUF_SIZE + 1, "now: %d-%d-%d %d:%d:%d\n",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

        if (sendto(sfd, snd_buf, strlen(snd_buf), 0, (const struct sockaddr *) cli_addr, addrlen) != strlen(snd_buf)) {
            perror("sendto");
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[]) {
    int sfd;
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
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC; // Allows IPv4 or IPv6
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV; // Use wildcard IP address

    if (getaddrinfo(NULL, SERVICE, &hints, &result) != 0)
        return -1;

    struct addrinfo *rp;

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // Success

        // bind() failed: close this socket and try next address
        close(sfd);
    }

    freeaddrinfo(result);

    if (rp == NULL) {
        fprintf(stderr, "Couldn't bind socket to any address\n");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        struct sockaddr_storage cli_addr;
        socklen_t addrlen = sizeof(cli_addr);

        char rcv_buf[BUF_SIZE + 1];
        ssize_t num_read;

        // receive message and save client's address
        num_read = recvfrom(sfd, rcv_buf, BUF_SIZE, 0, (struct sockaddr *) &cli_addr, &addrlen);
        if (num_read == -1) {
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }
        rcv_buf[num_read] = '\0';

        // Handle each client request in a new child process
        switch (fork()) {
            case -1:
                perror("fork");
                break; // give up on this request

            case 0: // Child
                handle_request(sfd, rcv_buf, &cli_addr, addrlen);
                exit(EXIT_SUCCESS);

            default: // Parent
                break; // loop to handle next request
        }
    }
}
