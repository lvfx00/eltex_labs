#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>

#include "chat_sysv_shmem.h"
#include "semun.h"
#include "server.h"
#include "sem_util.h"

static int serv_shmem_id; // server's shared memory segment identifier
static int serv_sem_id; // server's semaphore set identifier
static cli_msg *rcv_buf;

// for atexit()
static void exit_handler(void) {
    // remove server's semaphore set
    if(semctl(serv_sem_id, 0, IPC_RMID) == -1)
        perror("semctl IPC_RMID - server");

    // detach server's shared memory segment
    if(shmdt(rcv_buf) == -1)
        perror("shmdt - server");

    // delete server's shared memory segment
    if(shmctl(serv_shmem_id, IPC_RMID, NULL) == -1)
        perror("shmctl IPC_RMID - server");
}

// SIGINT handler
static void int_handler(int sig) {
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    client_info clients[MAX_CLIENTS_NUM];
    for (int i = 0; i < MAX_CLIENTS_NUM; ++i)
        clients[i].shmem_id = NO_ID; // set empty

    int clients_num = 0;

    // create server's semaphore set
    // 2 semaphores, 0th for reading access and 1st for writing access to server's shared memory segment
    // semaphore is set to 0 when reserved by any process for exclusive usage
    // semaphore is set to 1 when it is available for catching
    serv_sem_id = semget(SERVER_SEM_KEY, 2, IPC_CREAT | IPC_EXCL | S_IWUSR | S_IRUSR | S_IWGRP);
    if (serv_sem_id == -1) {
        perror("semget - server");
        exit(EXIT_FAILURE);
    }

    // initialize semaphores
    // set read access semaphore to 0 (prevent reading by server)
    if (init_sem_reserved(serv_sem_id, READ_SEM) == -1) {
        perror("init_sem_reserved - server");
        exit(EXIT_FAILURE);
    }

    // set write access semaphore to 1 (allow clients write messages to it)
    if (init_sem_released(serv_sem_id, WRITE_SEM) == -1) {
        perror("init_sem_released - server");
        exit(EXIT_FAILURE);
    }

    // create server's shared memory segment
    serv_shmem_id = shmget(SERVER_SHMEM_KEY, sizeof(cli_msg), IPC_CREAT | IPC_EXCL | S_IWUSR | S_IRUSR | S_IWGRP);
    if (serv_shmem_id == -1) {
        perror("shmget - server");
        exit(EXIT_FAILURE);
    }

    // attach it as buffer for receiving client's messages
    rcv_buf = shmat(serv_shmem_id, NULL, SHM_RDONLY);
    if (rcv_buf == (void *) -1) {
        perror("shmat - server");
        exit(EXIT_FAILURE);
    }

    // set exit handler to ensure that client's semaphore set will be removed
    // and shared memory segment will be detached
    if (atexit(exit_handler)) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    // set up handler for SIGINT, same as atexit
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = int_handler;
    if(sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        // wait for server's turn to read incoming message
        if (reserve_sem(serv_sem_id, READ_SEM, 0) == -1) {
            perror("reserve sem - server");
            exit(EXIT_FAILURE);
        }

        // print server log
        switch (rcv_buf->mtype) {
            case CLI_MT_LOGIN:
                printf("Got message of type CLI_MT_LOGIN from client %d (%s)\n",
                       rcv_buf->client_shmem_id, rcv_buf->nickname);
                break;
            case CLI_MT_LOGOUT:
                printf("Got message of type CLI_MT_LOGOUT from client %d\n",
                       rcv_buf->client_shmem_id);
                break;
            case CLI_MT_SEND_MSG:
                printf("Got message of type CLI_MT_SEND_MSG from client %d\nMessage content: %s\n",
                       rcv_buf->client_shmem_id, rcv_buf->msg_text);
                break;
            default:
                printf("Got invalid message from client %d\n",
                       rcv_buf->client_shmem_id);
                break;
        }

        // process input message
        switch (rcv_buf->mtype) {
            case CLI_MT_LOGIN: {
                // try to find client in logged in clients array by shared memory id
                client_info *info = find_client(rcv_buf->client_shmem_id, clients);
                if (info) break; // client is already in chat room, ignore message

                // attach client's shared memory segment
                serv_msg *snd_buf = shmat(rcv_buf->client_shmem_id, NULL, 0);
                if (snd_buf == (void *) -1) {
                    perror("shmat - client");
                    break;
                }

                // wait for server's turn to write message to client
                if (reserve_sem(rcv_buf->client_sem_id, WRITE_SEM, SEM_UNDO) == -1) {
                    if (shmdt(snd_buf) == -1) // detach previously attached client's shared memory segment
                        perror("shmdt - client");
                    perror("reserve_sem - client");
                    break;
                }

                // try to find empty structure to save client's info
                if (clients_num < MAX_CLIENTS_NUM && (info = find_empty(clients)) != NULL)
                    // write response to client's shared buffer upon successful logging in
                    snd_buf->mtype = SERV_MT_LOGIN_SUCCESS;
                else
                    // write response to client's shared buffer upon failure
                    snd_buf->mtype = SERV_MT_LOGIN_FAILURE;

                // allow client to read incoming message
                if (release_sem(rcv_buf->client_sem_id, READ_SEM, SEM_UNDO) == -1) {
                    if (shmdt(snd_buf) == -1)
                        perror("shmdt - client");
                    perror("release_sem - client");
                    break;
                }

                // save client's info in clients array if empty structure was found
                if (info != NULL) {
                    info->shmem_id = rcv_buf->client_shmem_id;
                    info->sem_id = rcv_buf->client_sem_id;
                    info->snd_buf = snd_buf; // save attached shared memory address
                    strcpy(info->nickname, rcv_buf->nickname);

                    clients_num++;

                    // send notification to another clients that this client logged in
                    serv_msg temp_buf;
                    temp_buf.mtype = SERV_MT_INFO_MSG;
                    snprintf(temp_buf.msg_text, MSG_TEXT_MAX_LEN + 1, "%s entered chat room.", info->nickname);
                    // other fields doesn't make any sense

                    send_to_all(&temp_buf, clients, &clients_num);
                }
                break;
            }

            case CLI_MT_LOGOUT: {
                client_info *info = find_client(rcv_buf->client_shmem_id, clients);
                if (!info) break; // if client not found, ignore message

                remove_client(info, clients, &clients_num);
                break;
            }

            case CLI_MT_SEND_MSG: {
                client_info *info = find_client(rcv_buf->client_shmem_id, clients);
                if (!info) break; // if client not found, ignore message

                // send received message to all clients in chat room
                serv_msg temp_buf;
                temp_buf.mtype = SERV_MT_USER_MSG;
                strcpy(temp_buf.nickname, info->nickname);
                strcpy(temp_buf.msg_text, rcv_buf->msg_text);

                send_to_all(&temp_buf, clients, &clients_num);
                break;
            }

            default:
                break; // ignore invalid message
        }

        // allow clients to write next message in server's shared memory
        if (release_sem(serv_sem_id, WRITE_SEM, 0) == -1) {
            perror("release_sem - server");
            exit(EXIT_FAILURE);
        }
    }
    exit(EXIT_SUCCESS);
}

client_info *find_client(int shmem_id, client_info *clients) {
    if (!clients || shmem_id < 0) return NULL;
    for (int i = 0; i < MAX_CLIENTS_NUM; ++i)
        if (clients[i].shmem_id == shmem_id)
            return &clients[i];
    return NULL;
}

client_info *find_empty(client_info *clients) {
    if (!clients) return NULL;
    for (int i = 0; i < MAX_CLIENTS_NUM; ++i)
        if (clients[i].shmem_id == NO_ID)
            return &clients[i];
    return NULL;
}

void send_to_all(const serv_msg *msg, client_info *clients, int *cli_num) {
    int count = *cli_num;
    for (int i = 0; i < MAX_CLIENTS_NUM; ++i) {
        if (clients[i].shmem_id != NO_ID) {
            // try to send message to client

            // wait for server's turn to write message to client
            if (reserve_sem(clients[i].sem_id, WRITE_SEM, SEM_UNDO) == -1) {
                perror("reserve_sem - client");
                remove_client(&clients[i], clients, cli_num);
                break;
            }

            // write message to client's shared memory buffer
            *clients[i].snd_buf = *msg;

            // allow client to read incoming message
            if (release_sem(clients[i].sem_id, READ_SEM, SEM_UNDO) == -1) {
                perror("release_sem - client");
                remove_client(&clients[i], clients, cli_num);
                break;
            }
        }
        if (count == 0) return;
    }
}

void remove_client(client_info *info, client_info *clients, int *cli_num) {
    // detach client's shared memory segment
    if (shmdt(info->snd_buf) == -1)
        perror("shmdt - client");
    // remove id from clients list (set free)
    info->shmem_id = NO_ID;

    // decrease number of clients
    (*cli_num)--;

    // send notification to another clients that this client logged out
    serv_msg temp_buf;
    temp_buf.mtype = SERV_MT_INFO_MSG;
    snprintf(temp_buf.msg_text, MSG_TEXT_MAX_LEN + 1, "%s disconnected from the server.", info->nickname);

    send_to_all(&temp_buf, clients, cli_num);
}

