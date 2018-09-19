#include <sys/msg.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "chat_sysv_msg.h"
#include "server.h"

static int serv_id; // server message queue identifier

static void int_handler(int sig) {
    if(msgctl(serv_id, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    client_info clients[MAX_CLIENTS_NUM];

    for (int i = 0; i < MAX_CLIENTS_NUM; ++i)
        clients[i].id = NO_ID; // set empty

    int clients_num = 0;

    cli_msg rcv_msg; // buffer for received messages
    ssize_t msg_len; // length of received message

    serv_msg snd_msg; // buffer for sending messages

    // create server message queue
    serv_id = msgget(SERVER_KEY, IPC_CREAT | IPC_EXCL | S_IWUSR | S_IRUSR | S_IWGRP);
    if (serv_id == -1) {
        perror("msgget - server msg queue");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = int_handler;
    if(sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        msg_len = msgrcv(serv_id, &rcv_msg, CLI_MSG_SIZE, 0, 0);
        if (msg_len == -1) {
            perror("msgrcv");
            break; // don't use exit() to close server's message queue after for cycle
        }

        printf("Got message of type \"%s\" from client %d\n",
               (rcv_msg.mtype == CLI_MT_LOGIN) ? "CLI_MT_LOGIN " :
               (rcv_msg.mtype == CLI_MT_LOGOUT) ? "CLI_MT_LOGOUT" : "CLI_MT_SEND_MSG", rcv_msg.client_id);
        if(rcv_msg.mtype == CLI_MT_LOGIN)
            printf("Nickname: %s\n", rcv_msg.nickname);
        if(rcv_msg.mtype == CLI_MT_SEND_MSG)
            printf("Message content: %s\n", rcv_msg.msg_text);

        // process input message
        switch (rcv_msg.mtype) {
            case CLI_MT_LOGIN: {
                // try to find client in logined clients array
                client_info *info = find_client(rcv_msg.client_id, clients);
                if (info) break; // client is already in chat room, ignore message

                // try to find empty structure to save client's info
                if (clients_num < MAX_CLIENTS_NUM && (info = find_empty(clients)) != NULL) {
                    info->id = rcv_msg.client_id;
                    strcpy(info->nickname, rcv_msg.nickname);
                    clients_num++;

                    snd_msg.mtype = SERV_MT_LOGIN_SUCCESS;
                    if (msgsnd(info->id, &snd_msg, 0, 0) == -1) { // send zero-length mtext info message
                        info->id = NO_ID; // remove client upon message sending failure
                        clients_num--;
                        break;
                    }

                    // send notification to another clients that this client logged in
                    snd_msg.mtype = SERV_MT_INFO_MSG;
                    snprintf(snd_msg.msg_text, MSG_TEXT_MAX_LEN + 1, "%s entered chat room.", info->nickname);
                    send_to_all(&snd_msg, SERV_MSG_SIZE, clients, &clients_num);

                } else { // if no empty structure found try to notify client about it
                    snd_msg.mtype = SERV_MT_LOGIN_FAILURE;
                    msgsnd(rcv_msg.client_id, &snd_msg, 0, 0);
                }
                break;
            }

            case CLI_MT_LOGOUT: {
                client_info *info = find_client(rcv_msg.client_id, clients);
                if (!info) break; // if client not found, ignore message

                // remove client
                info->id = NO_ID;
                clients_num--;

                // send notification to another clients that this client logged out
                snd_msg.mtype = SERV_MT_INFO_MSG;
                snprintf(snd_msg.msg_text, MSG_TEXT_MAX_LEN + 1, "%s leaved chat room.", info->nickname);
                send_to_all(&snd_msg, SERV_MSG_SIZE, clients, &clients_num);
                break;
            }

            case CLI_MT_SEND_MSG: {
                client_info *info = find_client(rcv_msg.client_id, clients);
                if (!info) break; // if client not found, ignore message

                // send received message to all clients in chat room
                snd_msg.mtype = SERV_MT_USER_MSG;
                strcpy(snd_msg.nickname, info->nickname);
                strcpy(snd_msg.msg_text, rcv_msg.msg_text);

                send_to_all(&snd_msg, SERV_MSG_SIZE, clients, &clients_num);
                break;
            }

            default:
                break; // ignore invalid message
        }
    }

    // remove server's message queue
    if(msgctl(serv_id, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

client_info *find_client(int id, client_info *clients) {
    if (!clients || id < 0) return NULL;
    for (int i = 0; i < MAX_CLIENTS_NUM; ++i)
        if (clients[i].id == id)
            return &clients[i];
    return NULL;
}

client_info *find_empty(client_info *clients) {
    if (!clients) return NULL;
    for (int i = 0; i < MAX_CLIENTS_NUM; ++i)
        if (clients[i].id == NO_ID)
            return &clients[i];
    return NULL;
}

void send_to_all(serv_msg *msg, size_t msg_size, client_info *clients, int *cli_num) {
    int count = *cli_num;
    for (int i = 0; i < MAX_CLIENTS_NUM; ++i) {
        if (clients[i].id != NO_ID) {
            if(msgsnd(clients[i].id, msg, msg_size, 0) == -1) {
                // delete disconnected client info
                clients[i].id = NO_ID;
                (*cli_num)--;

                // init msg for remaining clients about it
                serv_msg snd_msg;
                snd_msg.mtype = SERV_MT_INFO_MSG;
                snprintf(snd_msg.msg_text,
                        MSG_TEXT_MAX_LEN + 1, "%s disconnected from the server.", clients[i].nickname);
                send_to_all(&snd_msg, SERV_MSG_SIZE, clients, cli_num);
            }
            count--;
        }
        if (count == 0) return;
    }
}

