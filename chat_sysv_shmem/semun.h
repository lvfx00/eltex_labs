#pragma once

#include <sys/sem.h>
#include<sys/types.h>

// Used in calls to semctl()
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
#if defined(__linux__)
    struct seminfo *__buf;
#endif
};
