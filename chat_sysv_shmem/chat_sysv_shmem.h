#pragma once

#define SERVER_SEM_KEY 0x1aaaaaa1 // key for server's semaphore set
#define SERVER_SHMEM_KEY 0x1aaaaaa2 // key for server's shared memory

#define MSG_TEXT_MAX_LEN 512
#define NICKNAME_MAX_LEN 32

typedef struct cli_msg {
    long mtype; // One of CLI_MSG_TYPE_* values below
    int client_shmem_id; // using as client's id
    int client_sem_id;
    char nickname[NICKNAME_MAX_LEN + 1];
    char msg_text[MSG_TEXT_MAX_LEN + 1];
} cli_msg;

#define CLI_MT_LOGIN 1 // Purpose to enter in chat room
#define CLI_MT_SEND_MSG 2 // Send message
#define CLI_MT_LOGOUT 3 // Leave chat room

typedef struct serv_msg {
    long mtype; // One of SERV_MSG_TYPE_* values below
    char nickname[NICKNAME_MAX_LEN + 1];
    char msg_text[MSG_TEXT_MAX_LEN + 1];
} serv_msg;

#define SERV_MT_LOGIN_FAILURE 1 // Failed login
#define SERV_MT_LOGIN_SUCCESS 2 // Successful login
#define SERV_MT_USER_MSG 3 // User message from chat room
#define SERV_MT_INFO_MSG 4 // Info message from the server

#define WRITE_SEM 0
#define READ_SEM 1

