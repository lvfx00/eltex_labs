#pragma once

// TODO add semop flags usage

// Init semaphore "in use" - set it to 0 value
int init_sem_reserved(int sem_id, int sem_num);

// Init semaphore "available" - set it to 1 value
int init_sem_released(int sem_id, int sem_num);

// Reserve semaphore - decrement it by 1
// Flags argument can be received by ORing some of the following
// constants: IPC_NOWAIT, SEM_UNDO. Their effect is the same as
// in semop() function. If no flags required, 0 must be set.
int reserve_sem(int sem_id, int sem_num, short flags);

// Release semaphore - increment it by 1
int release_sem(int semId, int semNum, short flags);
