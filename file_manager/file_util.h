#pragma once

#include <stdlib.h>
#include <stdio.h>

struct copy_file_args {
    // File descriptors of source and destination files.
    int fd_src;
    int fd_dest;

    // Represents in percents copying progress. At the beginning
    // equals to 0(%), at the end equals to 100(%). If some error
    // occurs, status will be set to -1. Another thread can use it
    // to determine error.
    int *status;
};

char **get_fname_list(size_t *file_num);

void *copy_file(void *args);
