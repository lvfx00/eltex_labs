#include <errno.h>
#include <unistd.h>
#include "read_line.h"

ssize_t read_line(int fd, void *buffer, size_t bufsize) {
    ssize_t numRead;
    size_t totRead;
    char *buf;
    char ch;

    if (bufsize <= 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    buf = buffer;
    totRead = 0;
    for (;;) {
        numRead = read(fd, &ch, 1);
        if (numRead == -1) {
            if (errno == EINTR)
                continue;
            else
                return -1;

        } else if (numRead == 0) {
            if (totRead == 0)
                return 0;
            else
                break;

        } else {
            if (totRead < bufsize - 1) {
                totRead++;
                *buf++ = ch;
            }

            if (ch == '\n')
                break;
        }
    }

    *buf = '\0';
    return totRead;
}

