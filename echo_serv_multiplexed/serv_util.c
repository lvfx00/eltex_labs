#define _GNU_SOURCE

#include <errno.h>

#include "serv_util.h"

struct client_info *find_by_fd(int cfd, struct client_info *infos, size_t len) {
    if (!infos || len <= 0) {
        errno = EINVAL;
        return NULL;
    }

    for (int i = 0; i < len; ++i) {
        if (infos[i].sock_fd == cfd)
            return &infos[i];
    }

    // specified file descriptor not found
    errno = EEXIST;
    return NULL;
}

struct client_info *find_empty(struct client_info *infos, size_t len) {
    return find_by_fd(EMPTY_INFO, infos, len);
}

int remove_from_pollfds(int cfd, struct pollfd *poll_fds, int fdnum) {
    if (cfd <= 0 || !poll_fds || fdnum <= 0) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < fdnum; ++i) {
        if (poll_fds[i].fd == cfd) {
            // insert last pollfd structure to its place
            poll_fds[i] = poll_fds[fdnum - 1];
            poll_fds[fdnum - 1].fd = EMPTY_INFO;
            return 0;
        }
    }

    // specified file descriptor not found
    errno = ENOENT;
    return -1;
}
