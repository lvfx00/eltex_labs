#pragma once

#include <stddef.h> // for offsetof macro

#define SERVER_KEY 0x1aaaaaa1 // key for server's message queue

#define MSG_TEXT_MAX_LEN 512
#define NICKNAME_MAX_LEN 32

typedef struct cli_msg {
    long mtype; // One of CLI_MSG_TYPE_* values below
    int client_id;
    char nickname[NICKNAME_MAX_LEN + 1];
    char msg_text[MSG_TEXT_MAX_LEN + 1];
} cli_msg;

#define CLI_MT_LOGIN 1 // Purpose to enter in chat room
#define CLI_MT_SEND_MSG 2 // Send message
#define CLI_MT_LOGOUT 3 // Leave chat room

// handle possibility of padding bytes
#define CLI_MSG_SIZE (offsetof(struct cli_msg, msg_text) - offsetof(struct cli_msg, client_id) + MSG_TEXT_MAX_LEN)

typedef struct serv_msg {
    long mtype; // One of SERV_MSG_TYPE_* values below
    char nickname[NICKNAME_MAX_LEN + 1];
    char msg_text[MSG_TEXT_MAX_LEN + 1];
} serv_msg;

// handle possibility of padding bytes
#define SERV_MSG_SIZE (offsetof(struct serv_msg, msg_text) - offsetof(struct serv_msg, nickname) + MSG_TEXT_MAX_LEN)

#define SERV_MT_LOGIN_FAILURE 1 // Failed login
#define SERV_MT_LOGIN_SUCCESS 2 // Successful login
#define SERV_MT_USER_MSG 3 // User message from chat room
#define SERV_MT_INFO_MSG 4 // Info message from the server

