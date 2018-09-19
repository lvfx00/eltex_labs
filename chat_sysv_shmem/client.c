#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curses.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include "chat_sysv_shmem.h"
#include "client.h"
#include "sem_util.h"

static int cli_shmem_id; // identifier of client's shared memory segment
static int cli_sem_id; // identifier of client's semaphore set
static serv_msg *rcv_buf;
static cli_msg *snd_buf;

struct input_handler_args {
    WINDOW *chat_wnd;
    WINDOW *input_wnd;
    pthread_mutex_t *mutex;
};

// for atexit()
void exit_handler(void) {
    // exit from ncurses-mode
    endwin();

    // detach server's shared memory segment
    if(shmdt(snd_buf) == -1)
        perror("shmdt - server");

    // remove client's semaphore set
    if(semctl(cli_sem_id, 0, IPC_RMID) == -1)
        perror("semctl IPC_RMID - client");

    // detach client's shared memory segment
    if(shmdt(rcv_buf) == -1)
        perror("shmdt - client");

    // delete client's shared memory segment
    if(shmctl(cli_shmem_id, IPC_RMID, NULL) == -1)
        perror("shmctl IPC_RMID - client");
}

// SIGINT handler
void int_handler(int sig) {
    exit(EXIT_FAILURE);
}

void sig_winch(int signo) {
    struct winsize size;
    ioctl(fileno(stdout), TIOCGWINSZ, (char *) &size);
    resizeterm(size.ws_row, size.ws_col);
}

int main(int argc, char **argv) {
    int serv_sem_id; // identifier of server's shared memory segment
    int serv_shmem_id; // identifier of server's semaphore set

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

    // create client's semaphore set
    // 2 semaphores, 0th for reading access and 1st for writing access to client's shared memory segment
    // semaphore is set to 0 when reserved by any process for exclusive usage
    // semaphore is set to 1 when it is available for catching
    cli_sem_id = semget(IPC_PRIVATE, 2, IPC_CREAT | IPC_EXCL | S_IWUSR | S_IRUSR | S_IWGRP);
    if (cli_sem_id == -1) {
        perror("semget - client");
        exit(EXIT_FAILURE);
    }

    // initialize semaphores
    // set read access semaphore to 0 (prevent reading by client)
    if (init_sem_reserved(cli_sem_id, READ_SEM) == -1) {
        perror("init_sem_reserved - client");
        exit(EXIT_FAILURE);
    }
    // set write access semaphore to 1 (allow server to write messages to it)
    if (init_sem_released(cli_sem_id, WRITE_SEM) == -1) {
        perror("init_sem_released - client");
        exit(EXIT_FAILURE);
    }

    // create client's shared memory segment
    cli_shmem_id = shmget(IPC_PRIVATE, sizeof(serv_msg), IPC_CREAT | IPC_EXCL | S_IWUSR | S_IRUSR | S_IWGRP);
    if (cli_shmem_id == -1) {
        perror("shmget - client");
        exit(EXIT_FAILURE);
    }

    // and attach it
    rcv_buf = shmat(cli_shmem_id, NULL, SHM_RDONLY);
    if (rcv_buf == (void *) -1) {
        perror("shmat - client");
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

    // get server's semaphore set identifier
    serv_sem_id = semget(SERVER_SEM_KEY, 2, S_IWUSR | S_IRUSR);
    if (serv_sem_id == -1) {
        perror("semget - server");
        exit_with_msg(ctrl_wnd, "Unable to connect to the server.");
    }

    // get server's shared memory segment
    serv_shmem_id = shmget(SERVER_SHMEM_KEY, sizeof(cli_msg), S_IWUSR | S_IRUSR);
    if (serv_shmem_id == -1) {
        perror("shmget - server");
        exit_with_msg(ctrl_wnd, "Unable to connect to the server.");
    }

    // and attach it
    snd_buf = shmat(serv_shmem_id, NULL, 0);
    if (snd_buf == (void *) -1) {
        perror("shmat - server");
        exit_with_msg(ctrl_wnd, "Unable to connect to the server.");
    }

    // print enter nickname prompt
    wmove(ctrl_wnd, 0, 0);
    wprintw(ctrl_wnd, "Enter your nickname: ");
    echo();
    wgetnstr(ctrl_wnd, nickname, NICKNAME_MAX_LEN);
    noecho();

    // send login purpose message

    // wait for client's turn to write message to the server's shared memory buffer
    if (reserve_sem(serv_sem_id, WRITE_SEM, SEM_UNDO) == -1) {
        perror("reserve_sem - server WRITE_SEM");
        exit_with_msg(ctrl_wnd, "Unable to connect to the server.");
    }

    snd_buf->client_shmem_id = cli_shmem_id;
    snd_buf->client_sem_id = cli_sem_id;
    snd_buf->mtype = CLI_MT_LOGIN;
    strcpy(snd_buf->nickname, nickname);
    // snd_buf->msg_text doesn't make any sense

    // allow server to read incoming message
    if (release_sem(serv_sem_id, READ_SEM, SEM_UNDO) == -1) {
        perror("release_sem - server READ_SEM");
        exit_with_msg(ctrl_wnd, "Unable to connect to the server.");
    }

    int got_resp = 0;
    while (!got_resp) {
        // wait for client's turn to read message from its shared buffer
        if (reserve_sem(cli_sem_id, READ_SEM, 0) == -1) {
            perror("reserve_sem - client READ_SEM");
            exit(EXIT_FAILURE);
        }

        switch (rcv_buf->mtype) {
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

        // allow server to send new messages to client
        if (release_sem(cli_sem_id, WRITE_SEM, 0) == -1) {
            perror("release_sem - client WRITE_SEM");
            exit(EXIT_FAILURE);
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

                    // wait for client's turn to write message to the server's shared memory buffer
                    if (reserve_sem(serv_sem_id, WRITE_SEM, SEM_UNDO) == -1) {
                        perror("reserve - server WRITE");
                        exit_with_msg(ctrl_wnd, "Lost connection with the server.");
                    }

                    // write message to server's shared memory segment
                    snd_buf->client_shmem_id = cli_shmem_id;
                    snd_buf->mtype = CLI_MT_SEND_MSG;
                    strcpy(snd_buf->msg_text, message);
                    // other fields don't make any snse

                    // allow server to read incoming message
                    if (release_sem(serv_sem_id, READ_SEM, SEM_UNDO) == -1) {
                        perror("release_sem - server READ_SEM");
                        exit_with_msg(ctrl_wnd, "Lost connection with the server.");
                    }

                    count = 0; // empty current message buffer

                    // empty input message window
                    pthread_mutex_lock(&mutex);
                    werase(input_wnd);
                    wmove(input_wnd, 0, 0);
                    wechochar(input_wnd, '>');
                    wechochar(input_wnd, ' ');
                    wrefresh(input_wnd);
                    pthread_mutex_unlock(&mutex);
                }
                break;
                // exit from chat
            case KEY_F(10):
                // wait for client's turn to write message to the server's shared memory buffer
                if (reserve_sem(serv_sem_id, WRITE_SEM, SEM_UNDO) == -1) {
                    perror("reserve - server WRITE");
                    exit_with_msg(ctrl_wnd, "Lost connection with the server.");
                }

                // write logout message to server's shared memory buffer
                snd_buf->mtype = CLI_MT_LOGOUT;
                snd_buf->client_shmem_id = cli_shmem_id;
                // other fields don't make any snse

                // allow server to read incoming message
                if (release_sem(serv_sem_id, READ_SEM, SEM_UNDO) == -1) {
                    perror("release_sem - server READ_SEM");
                    exit_with_msg(ctrl_wnd, "Lost connection with the server.");
                }

                // cancel input handler thread
                if (pthread_cancel(th)) {
                    perror("pthread_cancel");
                    exit(EXIT_FAILURE);
                }
                if (pthread_join(th, NULL)) {
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

    int free_line_idx = 0; // index of first free line in chat window
    int rcv_msg_len; // for message length (in symbols)
    int req_str_num; // number of strings needed to print received message

    for (;;) {
        // get incoming messages from the server in non-blocking mode
        while (reserve_sem(cli_sem_id, READ_SEM, IPC_NOWAIT) != -1) {
            // process incoming message
            switch (rcv_buf->mtype) {
                case SERV_MT_USER_MSG:
                    // we may use (int) cast because received string don't exceed int type
                    rcv_msg_len = (int) (strlen(rcv_buf->nickname) + strlen(rcv_buf->msg_text) +
                                         2); // 2 for ": " between nick and msg
                    break;
                case SERV_MT_INFO_MSG:
                    rcv_msg_len = (int) (strlen(rcv_buf->msg_text));
                    break;
                default:
                    continue; // ignore messages of other type
            }

            pthread_mutex_lock(args->mutex);
            int y, x;
            getyx(args->input_wnd, y, x);

            req_str_num = rcv_msg_len / chat_wnd_width + (rcv_msg_len % chat_wnd_width != 0);
            // TODO process situation when str_to_add > chat_wnd_height
            // +1 to always have one empty line in bottom
            int str_to_add = (req_str_num + 1) - (chat_wnd_height - free_line_idx);
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
                free_line_idx -= str_to_add;
            }
            wmove(args->chat_wnd, free_line_idx, 0);

            switch (rcv_buf->mtype) {
                case SERV_MT_USER_MSG:
                    wprintw(args->chat_wnd, "%s: %s", rcv_buf->nickname, rcv_buf->msg_text);
                    break;
                case SERV_MT_INFO_MSG:
                    wprintw(args->chat_wnd, "%s", rcv_buf->msg_text);
                    break;
                default:
                    continue; // never reached
            }

            wrefresh(args->chat_wnd);
            free_line_idx += req_str_num;

            wmove(args->input_wnd, y, x);
            wrefresh(args->input_wnd);
            pthread_mutex_unlock(args->mutex);

            // allow server to write new message in client's buffer
            if (release_sem(cli_sem_id, WRITE_SEM, 0) == -1) {
                perror("release_sem - client WRITE_SEM");
                exit(EXIT_FAILURE);
            }
        }
        switch (errno) {
            case EAGAIN:
                nanosleep((const struct timespec[]) {{0, UPDATE_INTERVAL}}, NULL);
                break;
            default:
                perror("reserve_sem - client READ_SEM");
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
    exit(EXIT_FAILURE);
}
