#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <curses.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <pthread.h>
#include <fcntl.h>

#include "wnd_view.h"
#include "file_util.h"

#define ALT_KEY_ENTER 10

#define FILE_PANEL_COLOR 1
#define SELECT_COLOR 3
#define COPY_WND_COLOR 3
// find better system const
#define MAX_PATH 128

void sig_winch(int signo) {
    struct winsize size;
    ioctl(fileno(stdout), TIOCGWINSZ, (char *) &size);
    resizeterm(size.ws_row, size.ws_col);
}

int main(int argc, char **argv) {
    initscr();
    signal(SIGWINCH, sig_winch);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(FALSE);
    start_color();
    init_pair(FILE_PANEL_COLOR, COLOR_YELLOW, COLOR_BLUE);
    init_pair(2, COLOR_YELLOW, COLOR_RED);
    init_pair(SELECT_COLOR, COLOR_YELLOW, COLOR_RED);
    refresh();

    WINDOW *brdr_wins[2]; // windows for file panels borders
    WINDOW *fpnl_wins[2]; // file panels windows
    WINDOW *ctrl_win; // bottom control bar window
    size_t file_num[2]; // number of files in current directory
    char **file_name_list[2]; // names of files in cwd
    char cwd[2][PATH_MAX]; // cwd for each panel
    int curr_win = 0; // active panel number
    int pos[2] = {0, 0};// number of current selected file

    init_file_panels(brdr_wins, fpnl_wins, &ctrl_win, FILE_PANEL_COLOR, 2);

    // save cwd
    if (!getcwd(cwd[0], PATH_MAX)) {
        perror("getcwd");
        exit(EXIT_FAILURE);
    }
    strcpy(cwd[1], cwd[0]);

    // update panel contents
    file_name_list[0] = get_fname_list(&file_num[0]);
    refresh_file_list(file_name_list[0], file_num[0], fpnl_wins[0]);
    file_name_list[1] = get_fname_list(&file_num[1]);
    refresh_file_list(file_name_list[1], file_num[1], fpnl_wins[1]);

    // highlight selected file
    set_str_color(fpnl_wins[curr_win], pos[curr_win], SELECT_COLOR);

    int c;
    while ((c = getch()) != ERR) {
        switch (c) {
            case KEY_UP:
                if (pos[curr_win]) { // check if selected file already in upper string
                    // move highlighted line to up
                    set_str_color(fpnl_wins[curr_win], pos[curr_win], FILE_PANEL_COLOR);
                    set_str_color(fpnl_wins[curr_win], pos[curr_win] - 1, SELECT_COLOR);
                    --pos[curr_win];
                }
                break;

            case KEY_DOWN:
                if (pos[curr_win] != file_num[curr_win] - 1) {
                    set_str_color(fpnl_wins[curr_win], pos[curr_win], FILE_PANEL_COLOR);
                    set_str_color(fpnl_wins[curr_win], pos[curr_win] + 1, SELECT_COLOR);
                    ++pos[curr_win];
                }
                break;

            case '\t':
                // move highlighted line to another panel
                set_str_color(fpnl_wins[curr_win], pos[curr_win], FILE_PANEL_COLOR);
                // change current window
                curr_win = (curr_win + 1) % 2;
                set_str_color(fpnl_wins[curr_win], pos[curr_win], SELECT_COLOR);
                // set new cwd (that was opened in another panel)
                if (chdir(cwd[curr_win])) {
                    perror("chdir");
                    exit(EXIT_FAILURE);
                }
                break;

            case ALT_KEY_ENTER: {
                struct stat sb;
                if (stat(file_name_list[curr_win][pos[curr_win]], &sb)) {
                    perror("stat");
                    exit(EXIT_FAILURE);
                }
                switch (sb.st_mode & S_IFMT) {
                    // if selected file is a directory
                    case S_IFDIR:
                        // change current directory to new
                        if (chdir(file_name_list[curr_win][pos[curr_win]])) {
                            perror("chdir");
                            exit(EXIT_FAILURE);
                        }
                        // save new cwd for current window
                        if (!getcwd(cwd[curr_win], PATH_MAX)) {
                            perror("getcwd");
                            exit(EXIT_FAILURE);
                        }

                        // free previous filename data
                        for (int i = 0; i < file_num[curr_win]; ++i)
                            free(file_name_list[curr_win][i]);
                        free(file_name_list[curr_win]);


                        file_name_list[curr_win] = get_fname_list(&file_num[curr_win]);
                        refresh_file_list(file_name_list[curr_win], file_num[curr_win], fpnl_wins[curr_win]);

                        // move highlighted line to up
                        set_str_color(fpnl_wins[curr_win], pos[curr_win], FILE_PANEL_COLOR);
                        pos[curr_win] = 0;
                        set_str_color(fpnl_wins[curr_win], pos[curr_win], SELECT_COLOR);
                        refresh();
                        break;

                    case S_IFREG: {
                        endwin(); // exit from ncurses mode
                        // try to execute specified file
                        pid_t pid;
                        pid = fork();
                        if (-1 == pid) {
                            perror("fork");
                            exit(EXIT_FAILURE);
                        } else if (0 == pid) {
                            // try to execute regular file
                            // if fails, try to open in text editor
                            if (execl(file_name_list[curr_win][pos[curr_win]],
                                      file_name_list[curr_win][pos[curr_win]], NULL) == -1) {

                                if (execl("/bin/file_editor", "file_editor",
                                          file_name_list[curr_win][pos[curr_win]], NULL) == -1) {
                                    perror("execl");
                                    exit(EXIT_FAILURE);
                                }
                            }
                        }
                        // wait started process
                        int status;
                        if (waitpid(pid, &status, 0) == -1) {
                            perror("waitpid");
                            exit(EXIT_FAILURE);
                        }
                        // TODO write smthng in ctrl log???
                        initscr(); // return to ncurses mode
                        refresh();
                        break;
                    }

                    default:
                        break;
                }
                break;
            }
                // copy file
            case 'c': {
                struct stat sb;
                if (stat(file_name_list[curr_win][pos[curr_win]], &sb)) {
                    perror("stat");
                    exit(EXIT_FAILURE);
                }
                // check file type. Copy only regular files
                switch (sb.st_mode & S_IFMT) {
                    case S_IFREG: {
                        WINDOW *copy_box_win, *copy_win;
                        init_copy_win(&copy_box_win, &copy_win, COPY_WND_COLOR);
                        char pathname[MAX_PATH + 1];

                        wmove(copy_win, 1, 0);
                        echo(); // temporary turn echoing to see entered symbols
                        curs_set(TRUE); // temporary turn on cursor
                        wgetnstr(copy_win, pathname, MAX_PATH);
                        noecho();
                        curs_set(FALSE);
                        pathname[MAX_PATH] = '\0';

                        // variable to store copying progress
                        int status = 0;

                        // open source file
                        int fd_src = open(file_name_list[curr_win][pos[curr_win]], O_RDONLY);
                        // TODO process error output
                        if (fd_src == -1) {
                            perror("open");
                            exit(EXIT_FAILURE);
                        }

                        // TODO check if exists and provide overwrite dialog
                        // open destination file
                        int fd_dest = open(pathname, O_WRONLY | O_CREAT, 0644);
                        if (fd_dest == -1) {
                            perror("fopen");
                            exit(EXIT_FAILURE);
                        }

                        // init copy_func_args;
                        struct copy_file_args args;
                        args.fd_src = fd_src;
                        args.fd_dest = fd_dest;
                        args.status = &status;

                        // process copying
                        pthread_t copy_thread;
                        if (pthread_create(&copy_thread, NULL, copy_file, (void *) &args)) {
                            perror("pthread");
                            exit(EXIT_FAILURE);
                        }

                        // wait for file copying and update status
                        while (1) {
                            set_copying_progress(copy_win, status);
                            wrefresh(copy_win);

                            if (status == 100) {
                                pthread_detach(copy_thread);
                                wmove(ctrl_win, 0, 0);
                                wclrtoeol(ctrl_win);
                                wprintw(ctrl_win, "File \"%s\" successfully copied.",
                                        file_name_list[curr_win][pos[curr_win]]);
                                wrefresh(ctrl_win);
                                break;
                            }
                            if (status == -1) {
                                pthread_detach(copy_thread);
                                wmove(ctrl_win, 0, 0);
                                wclrtoeol(ctrl_win);
                                wprintw(ctrl_win, "Copy failed.");
                                wrefresh(ctrl_win);
                                break;
                            }
                            nanosleep((const struct timespec[]) {{0, 5000000L}}, NULL);
                        }

                        // remove copy dialog window
                        delwin(copy_win);
                        delwin(copy_box_win);

                        // redraw fpnl windows
                        for (int i = 0; i < 2; ++i) {
                            werase(brdr_wins[i]);
                            box(brdr_wins[i], '|', '-');
                            wrefresh(brdr_wins[i]);
                            file_name_list[i] = get_fname_list(&file_num[i]);
                            refresh_file_list(file_name_list[i], file_num[i], fpnl_wins[i]);
                        }
                        // set selection color
                        set_str_color(fpnl_wins[curr_win], pos[curr_win], SELECT_COLOR);
                        wrefresh(fpnl_wins[curr_win]);
                        break;
                    }

                    default:
                        break;
                }
                break;
            }

            case 'q':
                endwin();
                exit(EXIT_SUCCESS);

            default:
                break;
        }
    }
}