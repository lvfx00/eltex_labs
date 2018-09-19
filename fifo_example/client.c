#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "fifo_example.h"

void remove_fifo() {
    unlink(CLIENT_FIFO_PATHNAME);
}

int main() {
    int serv_fifo_fd;
    int client_fifo_fd;
    char request_msg[] = REQUEST_STRING;

    size_t bufsize = strlen(RESPONSE_STRING) + 1;
    char response_buf[bufsize];

    umask(0);
    if (mkfifo(CLIENT_FIFO_PATHNAME, S_IRUSR | S_IWUSR | S_IWGRP) == -1) {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }
    // add created fifo exit handler
    if(atexit(remove_fifo)) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    serv_fifo_fd = open(SERVER_FIFO_PATHNAME, O_WRONLY);
    if (serv_fifo_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    size_t request_msg_size = strlen(REQUEST_STRING) + 1;
    if (write(serv_fifo_fd, request_msg, request_msg_size) != request_msg_size) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    client_fifo_fd = open(CLIENT_FIFO_PATHNAME, O_RDONLY);
    if (client_fifo_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    if (read(client_fifo_fd, response_buf, bufsize) != bufsize) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    printf("Recieved response: %s\n", response_buf);
    exit(EXIT_SUCCESS);
}

