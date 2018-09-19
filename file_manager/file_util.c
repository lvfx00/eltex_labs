#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "file_util.h"

#define START_MEM_SIZE 16
#define INC_FACTOR 2
#define BUF_SIZE 8192

char **get_fname_list(size_t *file_num) {
    size_t curr_size = sizeof(char *) * START_MEM_SIZE;
    size_t file_count = 0;

    char **fname_list = malloc(curr_size);
    if (!fname_list) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }

    // open current directory
    DIR *cwd = opendir(".");
    if(!cwd) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    errno = 0; // reset errno to prevent error msg missing
    while((entry = readdir(cwd)) != NULL) {
        // realloc array if it doesn't have enough memory to store all file names
        if(!(curr_size - file_count * sizeof(char *))) {
            curr_size *= INC_FACTOR;
            fname_list = realloc(fname_list, curr_size);
            if(!fname_list) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
        }

        size_t name_len = strlen(entry->d_name);
        if(!(fname_list[file_count] = malloc(name_len + 1))) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        strcpy(fname_list[file_count], entry->d_name);
        ++file_count;
    }
    if(errno && !entry) {
        perror("readdir");
        exit(EXIT_FAILURE);
    }

    if(closedir(cwd)) {
        perror("closedir");
        exit(EXIT_FAILURE);
    }

    *file_num = file_count;
    return fname_list;
}

void *copy_file(void *arg) {
    struct copy_file_args *args = (struct copy_file_args *) arg;

    // get src file size
    off_t file_size = lseek(args->fd_src, 0, SEEK_END);
    if(file_size == -1) {
        *(args->status) = -1; // set error status
        perror("lseek");
        pthread_exit(arg);
    }
    // return file position to beginning
    if(lseek(args->fd_src, 0 ,SEEK_SET) == -1) {
        *(args->status) = -1;
        perror("lseek");
        pthread_exit(arg);
    }

    char buf[BUF_SIZE];
    size_t bytes_copied = 0;
    *(args->status) = 0;

    while (1) {
        ssize_t r_count = read(args->fd_src, buf, BUF_SIZE);
        if (!r_count) {
            break;
        }
        if(r_count == -1) {
            *(args->status) = -1;
            perror("read");
            pthread_exit(arg);
        }

        if(write(args->fd_dest, buf, (size_t)r_count) == -1) {
            *(args->status) = -1;
            perror("write");
            pthread_exit(arg);
        }
        bytes_copied += r_count;
        //update status info
        *(args->status) = (int) ((bytes_copied * 100) / file_size);
    }
    pthread_exit(arg);
}
