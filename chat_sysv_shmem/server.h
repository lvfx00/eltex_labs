#pragma once

#include "chat_sysv_shmem.h"

#define NO_ID -1 // to mark empty structure

#define MAX_CLIENTS_NUM 64

// struct to store message queue ids with corresponding nicknames
typedef struct client_info {
    int shmem_id;
    int sem_id;
    serv_msg *snd_buf;
    char nickname[NICKNAME_MAX_LEN + 1];
} client_info;

// Tries to find client with specified shared memory id in clients array.
// Returns pointer to related client structure upon success and NULL
// pointer upon failure.
client_info *find_client(int shmem_id, client_info *clients);

// Tries to find empty structure (with shmem_id set to NO_ID) in clients array.
// Returns pointer to found empty client structure upon success and NULL
// pointer upon failure.
client_info *find_empty(client_info *clients);

// Sends specified message to at most cli_num clients in clients array,
// Typically, cli_num is number of all existing clients in array.
// If one of clients was disconnected (or something happened that server have to
// remove client from list), corresponding message will be sent to remaining clients
// and data of disconnected client will be removed from clients array.
void send_to_all(const serv_msg *msg_buf, client_info *clients, int *cli_num);

// removes specified client from clients list, detaches client's shared memory segment
// and notifies remaining clients about it
void remove_client(client_info *info, client_info *clients, int *cli_num);
