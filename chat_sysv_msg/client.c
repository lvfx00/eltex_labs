#include <stdio.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curses.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>

#include "chat_sysv_msg.h"
#include "client.h"

// TODO remove static field???
static int client_id;

struct input_handler_args {
    WINDOW *chat_wnd;
    WINDOW *input_wnd;
    pthread_mutex_t *mutex;
    pthread_t *main_thread;
};

// for atexit()
static void remove_queue(void) {
    // exit from ncurses-mode
    endwin();
    // close client's message queue
    if (msgctl(client_id, IPC_RMID, NULL) == -1)
        perror("msgctl");
}

// SIGINT handler
static void int_handler(int sig) {
    remove_queue();
}

void sig_winch(int signo) {
    struct winsize size;
    ioctl(fileno(stdout), TIOCGWINSZ, (char *) &size);
    resizeterm(size.ws_row, size.ws_col);
}

int main(int argc, char **argv) {
    int serv_id; // identifier of server's message queue

    serv_msg rcv_msg; // buffer for received messages
    cli_msg snd_msg; // buffer for sending messages

    char nickname[NICKNAME_MAX_LEN + 1]; // client's nickname buffer

    // init ncurses
    initscr();
    signal(SIGWINCH, sig_winch);
    cbreak();
    noecho();
    start_color();
    init_pair(CHAT_WND_COLOR, COLOR_YELLOW, COLOR_BLUE);
    init_pair(INPUT_WND_COLOR, COLOR_WHITE, COLOR_BLUE);
    init_pair(CTRL_WND_COLOR, COLOR_YELLOW, COLOR_RED);
    refresh();

    WINDOW *chat_brdr_wnd; // window for chat borders
    WINDOW *chat_wnd; // window for chat room messages
    WINDOW *input_wnd; // window for client's message input
    WINDOW *ctrl_wnd; // window for control options and status messages

    init_file_panels(&chat_brdr_wnd, &chat_wnd, &input_wnd, &ctrl_wnd);

    // create client message queue
    client_id = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | S_IWUSR | S_IRUSR | S_IWGRP);
    if (client_id == -1) {
        perror("msgget - client message queue");
        exit(EXIT_FAILURE);
    }

    // set exit handler to ensure that client message queue will be deleted
    if (atexit(remove_queue)) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    // set up handler for SIGINT
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = int_handler;
    if(sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // get server's message queue identifier
    serv_id = msgget(SERVER_KEY, S_IWUSR);
    if (serv_id == -1)
        exit_with_msg(ctrl_wnd, "Unable to connect to the server.");

    wmove(ctrl_wnd, 0, 0);
    wprintw(ctrl_wnd, "Enter your nickname: ");
    echo();
    wgetnstr(ctrl_wnd, nickname, NICKNAME_MAX_LEN);
    noecho();

    // send login purpose message
    snd_msg.client_id = client_id;
    snd_msg.mtype = CLI_MT_LOGIN;
    strcpy(snd_msg.nickname, nickname);

    if (msgsnd(serv_id, &snd_msg, CLI_MSG_SIZE, 0) == -1)
        exit_with_msg(ctrl_wnd, "Unable to connect to the server.");

    int got_resp = 0;
    while (!got_resp) {
        if (msgrcv(client_id, &rcv_msg, SERV_MSG_SIZE, 0, 0) == -1) {
            perror("msgrcv");
            exit(EXIT_FAILURE);
        }
        switch (rcv_msg.mtype) {
            case SERV_MT_LOGIN_SUCCESS:
                // print success message
                wmove(ctrl_wnd, 0, 0);
                wclrtoeol(ctrl_wnd);
                wprintw(ctrl_wnd, "Successfully logged in chat room as %s.", nickname);
                wrefresh(ctrl_wnd);
                // and prompt
                wmove(input_wnd, 0, 0);
                wechochar(input_wnd, '>');
                wechochar(input_wnd, ' ');
                wrefresh(input_wnd);
                got_resp = 1;
                break;

            case SERV_MT_LOGIN_FAILURE:
                exit_with_msg(ctrl_wnd, "Server is busy, try again later.");

            default:
                break; // ignore other messages
        }
    }

    // Mutex is used to prevent interruption by another thread during editing
    // windows content. In this case shared resource is cursor position.
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    struct input_handler_args args = {chat_wnd, input_wnd, &mutex};
    // create thread for incoming server messages handling
    pthread_t th;
    if (pthread_create(&th, NULL, input_handler, (void *) &args)) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    char message[MSG_TEXT_MAX_LEN + 1]; // buffer for current message in text editor
    int count = 0; // number of entered characters

    // save size of input window
    int input_wnd_width, input_wnd_height;
    getmaxyx(input_wnd, input_wnd_height, input_wnd_width);

    // get symbol from input
    int c;
    while ((c = wgetch(input_wnd)) != EOF) {
        switch (c) {
            // backspace option
            case ALT_KEY_BACKSPACE:
                if (count > 0) {
                    pthread_mutex_lock(&mutex);
                    int x, y;
                    getyx(input_wnd, y, x);
                    // move to previous line, if cursor reached left window border
                    if (x == 0 && y > 0) {
                        mvwaddch(input_wnd, y - 1, input_wnd_width - 1, ' ');
                        wmove(input_wnd, y - 1, input_wnd_width - 1);
                    }
                    mvwaddch(input_wnd, y, x - 1, ' ');
                    wmove(input_wnd, y, x - 1);
                    wrefresh(input_wnd);
                    pthread_mutex_unlock(&mutex);
                    count--;
                }
                break;
                // send entered message
            case ALT_KEY_ENTER:
                if (count > 0) {
                    message[count] = '\0'; // terminate message string
                    snd_msg.client_id = client_id;
                    snd_msg.mtype = CLI_MT_SEND_MSG;
                    strcpy(snd_msg.msg_text, message);

                    // send message
                    if (msgsnd(serv_id, &snd_msg, CLI_MSG_SIZE, 0) == -1)
                        exit_with_msg(ctrl_wnd, "Lost connection with the server.");
                    count = 0;

                    // empty input message window
                    pthread_mutex_lock(&mutex);
                    werase(input_wnd);
                    wmove(input_wnd, 0, 0);
                    wechochar(input_wnd, '>');
                    wechochar(input_wnd, ' ');
                    wrefresh(input_wnd);
                    pthread_mutex_unlock(&mutex);
                    break;
                }
                // exit from chat
            case KEY_F(10):
                // send logout message
                snd_msg.mtype = CLI_MT_LOGOUT;
                snd_msg.client_id = client_id;
                msgsnd(serv_id, &snd_msg, CLI_MSG_SIZE, 0); // can don't check result

                // cancel input handler thread
                if (pthread_cancel(th)) {
                    perror("pthread_cancel");
                    exit(EXIT_FAILURE);
                }
                if(pthread_join(th, NULL)) {
                    perror("pthread_join");
                    exit(EXIT_FAILURE);
                }
                exit(EXIT_SUCCESS);

            default:
                // if symbol is printable add symbol to message
                if (isprint(c) && count < MSG_TEXT_MAX_LEN) {
                    message[count] = (char) c;
                    count++;
                    // and print it
                    pthread_mutex_lock(&mutex);
                    wechochar(input_wnd, c);
                    pthread_mutex_unlock(&mutex);
                }
                // otherwise ignore it
                break;
        }
    }
}

void init_file_panels(WINDOW **chat_brdr_wnd, WINDOW **chat_wnd, WINDOW **input_wnd, WINDOW **ctrl_wnd) {
    struct winsize size;
    ioctl(fileno(stdout), TIOCGWINSZ, (char *) &size);

    // create chat border window
    *chat_brdr_wnd = newwin(size.ws_row - INPUT_WND_HEIGHT - CTRL_WND_HEIGHT, size.ws_col, 0, 0);
    if (!(*chat_brdr_wnd)) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wbkgd(*chat_brdr_wnd, COLOR_PAIR(CHAT_WND_COLOR));
    wattron(*chat_brdr_wnd, A_BOLD);
    box(*chat_brdr_wnd, '|', '-');
    wrefresh(*chat_brdr_wnd);

    // create chat window
    *chat_wnd = derwin(*chat_brdr_wnd, size.ws_row - INPUT_WND_HEIGHT - CTRL_WND_HEIGHT - 2, size.ws_col - 2, 1, 1);
    if (!(*chat_wnd)) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wrefresh(*chat_wnd);

    // create bottom input message window
    *input_wnd = newwin(INPUT_WND_HEIGHT, size.ws_col, size.ws_row - INPUT_WND_HEIGHT - CTRL_WND_HEIGHT, 0);
    if (!(*input_wnd)) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    keypad(*input_wnd, TRUE);
    wbkgd(*input_wnd, COLOR_PAIR(INPUT_WND_COLOR));
    wattron(*input_wnd, A_BOLD);
    wrefresh(*input_wnd);

    // create bottom input message window
    *ctrl_wnd = newwin(CTRL_WND_HEIGHT, size.ws_col, size.ws_row - CTRL_WND_HEIGHT, 0);
    if (!(*ctrl_wnd)) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wbkgd(*ctrl_wnd, COLOR_PAIR(CTRL_WND_COLOR));
    wattron(*ctrl_wnd, A_BOLD);
    wmove(*ctrl_wnd, 2, 0);
    wrefresh(*ctrl_wnd);
    wprintw(*ctrl_wnd, "ENTER - send message\tF10 - quit");
    wrefresh(*ctrl_wnd);
}

void *input_handler(void *arg) {
    struct input_handler_args *args = (struct input_handler_args *) arg;

    // get size of chat window
    int chat_wnd_width, chat_wnd_height;
    getmaxyx(args->chat_wnd, chat_wnd_height, chat_wnd_width);

    serv_msg rcv_msg; // buffer for received messages

    int fline_idx = 0; // index of first free line in chat window
    int rcv_msg_len; // for message length (in symbols)
    int req_str_num; // number of strings needed to print received message

    for (;;) {
        // get incoming messages from the server in non-blocking mode
        // while there are messages in queue process them
        while (msgrcv(client_id, &rcv_msg, SERV_MSG_SIZE, 0, IPC_NOWAIT) != -1) {
            switch (rcv_msg.mtype) {
                case SERV_MT_USER_MSG:
                    // we may use (int) cast because received string don't exceed int type
                    rcv_msg_len = (int) (strlen(rcv_msg.nickname) + strlen(rcv_msg.msg_text) +
                                         2); // 2 for ": " between nick and msg
                    break;
                case SERV_MT_INFO_MSG:
                    rcv_msg_len = (int) (strlen(rcv_msg.msg_text));
                    break;
                default:
                    // ignore messages of other type
                    continue; // continue while loop
            }

            pthread_mutex_lock(args->mutex);
            int y, x;
            getyx(args->input_wnd, y, x);

            req_str_num = rcv_msg_len / chat_wnd_width + (rcv_msg_len % chat_wnd_width != 0);
            // TODO process situation when str_to_add > chat_wnd_height
            // +1 to always have one empty line in bottom
            int str_to_add = (req_str_num + 1) - (chat_wnd_height - fline_idx);
            if (str_to_add > 0) {
                for (int i = 0; i < chat_wnd_height; ++i) {
                    char str_buf[chat_wnd_width + 1];
                    if (i + str_to_add < chat_wnd_height) {
                        // copy string
                        mvwinnstr(args->chat_wnd, i + str_to_add, 0, str_buf, chat_wnd_width);
                        wmove(args->chat_wnd, i, 0);
                        wclrtoeol(args->chat_wnd);
                        wprintw(args->chat_wnd, "%s", str_buf);

                    } else { // erase string
                        wmove(args->chat_wnd, i, 0);
                        wclrtoeol(args->chat_wnd);
                    }
                }
                fline_idx -= str_to_add;
            }
            wmove(args->chat_wnd, fline_idx, 0);

            switch (rcv_msg.mtype) {
                case SERV_MT_USER_MSG:
                    wprintw(args->chat_wnd, "%s: %s", rcv_msg.nickname, rcv_msg.msg_text);
                    break;
                case SERV_MT_INFO_MSG:
                    wprintw(args->chat_wnd, "%s", rcv_msg.msg_text);
                    break;
                default:
                    continue; // never reached
            }

            wrefresh(args->chat_wnd);
            fline_idx += req_str_num;

            wmove(args->input_wnd, y, x);
            wrefresh(args->input_wnd);
            pthread_mutex_unlock(args->mutex);
        }
        switch (errno) {
            case ENOMSG:
                nanosleep((const struct timespec[]) {{0, UPDATE_INTERVAL}}, NULL);
                break;
            default:
                perror("msgrcv");
                exit(EXIT_FAILURE);
        }
    }
}

void exit_with_msg(WINDOW *wnd, char *msg) {
    wmove(wnd, 0, 0);
    wclrtoeol(wnd);
    wprintw(wnd, msg);
    wmove(wnd, 1, 0);
    wprintw(wnd, "Press any key to exit...");
    wrefresh(wnd);
    getch();
    exit(EXIT_SUCCESS);
}
