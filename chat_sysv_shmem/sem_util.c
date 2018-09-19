#include <errno.h>
#include "sem_util.h"
#include "semun.h"

int init_sem_released(int sem_id, int sem_num) {
    union semun arg;
    arg.val = 1;
    return semctl(sem_id, sem_num, SETVAL, arg);
}

int init_sem_reserved(int sem_id, int sem_num) {
    union semun arg;
    arg.val = 0;
    return semctl(sem_id, sem_num, SETVAL, arg);
}

int reserve_sem(int sem_id, int sem_num, short flags) {
    struct sembuf sops;
    sops.sem_num = sem_num;
    sops.sem_op = -1;
    sops.sem_flg = flags;
    while (semop(sem_id, &sops, 1) == -1) // using loop to handle interruption by signal
        if (errno != EINTR)
            return -1;
    return 0;
}

int release_sem(int semId, int semNum, short flags) {
    struct sembuf sops;
    sops.sem_num = semNum;
    sops.sem_op = 1;
    sops.sem_flg = flags;
    return semop(semId, &sops, 1);
}
