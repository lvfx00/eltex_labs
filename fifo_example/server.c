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
    unlink(SERVER_FIFO_PATHNAME);
}

int main() {
    int serv_fifo_fd;
    char resp[] = RESPONSE_STRING;

    size_t bufsize = strlen(REQUEST_STRING) + 1;
    char request_buf[bufsize];

    umask(0);
    if (mkfifo(SERVER_FIFO_PATHNAME, S_IRUSR | S_IWUSR | S_IWGRP) == -1) {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }
    if (atexit(remove_fifo)) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    serv_fifo_fd = open(SERVER_FIFO_PATHNAME, O_RDONLY);
    if (serv_fifo_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    if (read(serv_fifo_fd, request_buf, bufsize) != bufsize) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    printf("Recieved request: %s\n", request_buf);

    if (strcmp(request_buf, REQUEST_STRING) == 0) {
        int client_fifo_fd = open(CLIENT_FIFO_PATHNAME, O_WRONLY);
        if (client_fifo_fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        size_t resp_str_size = strlen(resp) + 1;
        if(write(client_fifo_fd, resp, resp_str_size) != resp_str_size) {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }
    exit(EXIT_SUCCESS);
}

