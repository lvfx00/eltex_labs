#include <curses.h>
#include <sys/ioctl.h>
#include <stdlib.h>

#include "wnd_view.h"

#define CTRL_BAR_ROWS_NUM 3
#define MAX_LINE_LEN 128

#define COPY_WIN_HEIGHT 4
#define COPY_WIN_H_PADDINGS 16

//returns 0 upon success and integer value other than 0 upon error
void init_file_panels(WINDOW *brdr_wins[2], WINDOW *fpnl_wins[2], WINDOW **ctrl_win, int fpnl_color, int ctrl_color) {

    struct winsize size;
    ioctl(fileno(stdout), TIOCGWINSZ, (char *) &size);

    // create border windows
    // if terminal window has odd number of columns the first panel will be greater on 1 column
    brdr_wins[0] = newwin(size.ws_row - CTRL_BAR_ROWS_NUM,
                          (size.ws_col % 2) ? size.ws_col / 2 + 1 : size.ws_col / 2,
                          0,
                          0);

    if (!brdr_wins[0]) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wbkgd(brdr_wins[0], COLOR_PAIR(fpnl_color));
    wattron(brdr_wins[0], A_BOLD);
    box(brdr_wins[0], '|', '-');
    wrefresh(brdr_wins[0]);

    brdr_wins[1] = newwin(size.ws_row - CTRL_BAR_ROWS_NUM,
                          size.ws_col / 2,
                          0,
                          (size.ws_col % 2) ? size.ws_col / 2 + 1 : size.ws_col / 2);

    if (!brdr_wins[1]) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wbkgd(brdr_wins[1], COLOR_PAIR(fpnl_color));
    wattron(brdr_wins[1], A_BOLD);
    box(brdr_wins[1], '|', '-');
    wrefresh(brdr_wins[1]);

    // create file panels
    fpnl_wins[0] = derwin(brdr_wins[0],
                          size.ws_row - CTRL_BAR_ROWS_NUM - 2,
                          (size.ws_col % 2) ? size.ws_col / 2 - 1 : size.ws_col / 2 - 2,
                          1,
                          1);

    if (!fpnl_wins[0]) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wrefresh(fpnl_wins[0]);

    fpnl_wins[1] = derwin(brdr_wins[1],
                          size.ws_row - CTRL_BAR_ROWS_NUM - 2,
                          size.ws_col / 2 - 2,
                          1,
                          1);

    if (!fpnl_wins[1]) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wrefresh(fpnl_wins[1]);

    // create bottom control bar
    *ctrl_win = newwin(CTRL_BAR_ROWS_NUM,
                       size.ws_col,
                       size.ws_row - CTRL_BAR_ROWS_NUM,
                       0);
    if (!(*ctrl_win)) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wbkgd(*ctrl_win, COLOR_PAIR(ctrl_color));
    wattron(*ctrl_win, A_BOLD);
    wmove(*ctrl_win, 2, 0);
    wrefresh(*ctrl_win);
    wprintw(*ctrl_win, "UP DOWN - navigate\tTAB - change panel\tENTER - open\tc - copy\tq - exit");
    wrefresh(*ctrl_win);
}

// updates file view of file_list in specified window
void refresh_file_list(char **file_list, size_t file_num, WINDOW *win) {
    // remove previous file panel content
    werase(win);
    wmove(win, 0, 0);

    if (!file_list) {
        perror("invalid file_list pointer");
        exit(EXIT_FAILURE);
    }

    // TODO implement scrolling
    int x, y;
    getmaxyx(win, y, x);
    for (int i = 0; i < file_num && i < y; ++i) {
        if (!file_list[i]) {
            perror("invalid pointer to filename");
            exit(EXIT_FAILURE);
        }
        wprintw(win, "%.*s\n", x, file_list[i]);
    }
    wrefresh(win);
}

void set_str_color(WINDOW *win, int str_num, int color) {
    char strbuf[MAX_LINE_LEN + 1];
    wmove(win, str_num, 0);
    winnstr(win, strbuf, MAX_LINE_LEN);
    strbuf[MAX_LINE_LEN] = '\0';

    wattron(win, COLOR_PAIR(color));
    wprintw(win, "%s", strbuf);
    wattroff(win, COLOR_PAIR(color));

    wrefresh(win);
}

void init_copy_win(WINDOW **copy_box_win, WINDOW **copy_win, int color) {
    // get terminal size
    struct winsize size;
    ioctl(fileno(stdout), TIOCGWINSZ, (char *) &size);

    // create borders
    *copy_box_win = newwin(COPY_WIN_HEIGHT + 2,
                           size.ws_col - COPY_WIN_H_PADDINGS * 2,
                           (size.ws_row - COPY_WIN_HEIGHT - 2) / 2,
                           COPY_WIN_H_PADDINGS);
    if (!(*copy_box_win)) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }

    wbkgd(*copy_box_win, COLOR_PAIR(color));
    wattron(*copy_box_win, A_BOLD);
    box(*copy_box_win, '*', '*');
    wrefresh(*copy_box_win);

    // create copy file dialog window
    *copy_win = derwin(*copy_box_win,
                       COPY_WIN_HEIGHT,
                       size.ws_col - COPY_WIN_H_PADDINGS * 2 - 2,
                       1,
                       1);
    if (!(*copy_win)) {
        perror("newwin");
        exit(EXIT_FAILURE);
    }
    wrefresh(*copy_win);

    wmove(*copy_win, 0, 0);
    wprintw(*copy_win, "Copy to:");
    wrefresh(*copy_win);
}

// | [****   ] 100%| progress bar view
void set_copying_progress(WINDOW *copy_win, int progress) {
    // invalid progress
    if (progress < 0 || progress > 100) return;

    int length, height; // to store copy dialog window size
    getmaxyx(copy_win, height, length);

    // print beginning of status bar
    wmove(copy_win, 2, 1);
    wechochar(copy_win, '[');
    wmove(copy_win, 2, 2);

    // calc number of asterisks to print
    int bar_len = length - 9;
    int aster_num = bar_len * progress / 100;

    // print asteriks
    for (int i = 0; i < aster_num; ++i)
        wechochar(copy_win, '*');
    // print empty part
    for (int i = 0; i < bar_len - aster_num; ++i)
        wechochar(copy_win, ' ');

    // print percents and ending of status bar
    wprintw(copy_win, "] %d\%", progress);
    wrefresh(copy_win);
}
