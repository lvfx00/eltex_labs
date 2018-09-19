#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdlib.h>
#include <curses.h>
#include <ctype.h>

#define CNTRL_ROWS_NUM 2
#define MAX_LINE_LEN 100
// TODO find out why KEY_BACKSPACE not working
#define ALT_KEY_BACKSPACE 127
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
    curs_set(TRUE);

    start_color();
    init_pair(1, COLOR_YELLOW, COLOR_BLUE);
    init_pair(2, COLOR_YELLOW, COLOR_RED);
    refresh();

    //get size of term
    struct winsize size;
    ioctl(fileno(stdout), TIOCGWINSZ, (char *) &size);

    // window for control options
    WINDOW *control_win;
    control_win = newwin(CNTRL_ROWS_NUM, size.ws_col, size.ws_row - CNTRL_ROWS_NUM, 0);
    wbkgd(control_win, COLOR_PAIR(2));
    wattron(control_win, A_BOLD);
    wmove(control_win, 1, 0);
    wprintw(control_win, "F5 - open\tF6 - save\tq - exit");
    wrefresh(control_win);

    // main window for text edit
    WINDOW *editor_win;
    editor_win = newwin(size.ws_row - CNTRL_ROWS_NUM, size.ws_col, 0, 0);
    keypad(editor_win, TRUE);
    wbkgd(editor_win, COLOR_PAIR(1));
    wattron(editor_win, A_BOLD);
    wmove(editor_win, 0, 0);
    wrefresh(editor_win);

    // TODO split opening file in new function
    // try to open specified in command line arguments file

    if (argc > 1) {
        int x, y;
        getyx(editor_win, y, x);
        // opening file to read
        FILE *stream;
        stream = fopen(argv[1], "r");
        if (!stream) {
            wmove(control_win, 0, 0);
            wclrtoeol(control_win);
            wprintw(control_win, "Unable to open file \"%s\".", argv[1]);
            wrefresh(control_win);
            // return cursor back
            wmove(editor_win, y, x);
        } else {
            werase(editor_win); // erase all symbols on editor screen for new file
            wmove(editor_win, 0, 0);
            int ch;
            // printing file contents
            while ((ch = fgetc(stream)) != EOF) {
                waddch(editor_win, ch);
            }

            //close file
            if (fclose(stream) == EOF) {
                perror("fclose");
                return 1;
            }

            wmove(control_win, 0, 0);
            wclrtoeol(control_win);
            wprintw(control_win, "File \"%s\" succesfully opened.", argv[1]);
            wrefresh(control_win);

            wmove(editor_win, 0, 0);
            wrefresh(editor_win);
        }
    }

    int c;
    while ((c = wgetch(editor_win)) != ERR) {
        switch (c) {
            case ALT_KEY_BACKSPACE: {
                int x, y;
                getyx(editor_win, y, x);
                if (x == 0) continue;
                mvwaddch(editor_win, y, x - 1, ' ');
                wmove(editor_win, y, x - 1);
                wrefresh(editor_win);
                break;
            }

            case KEY_LEFT: {
                int x, y;
                getyx(editor_win, y, x);
                if (x == 0) continue;
                wmove(editor_win, y, x - 1);
                wrefresh(editor_win);
                break;
            }

            case KEY_RIGHT: {
                int x, y;
                getyx(editor_win, y, x);
                if (x == size.ws_col - 1) continue;
                wmove(editor_win, y, x + 1);
                wrefresh(editor_win);
                break;
            }

            case KEY_UP: {
                int x, y;
                getyx(editor_win, y, x);
                if (y == 0) continue;
                wmove(editor_win, y - 1, x);
                wrefresh(editor_win);
                break;
            }

            case KEY_DOWN: {
                int x, y;
                getyx(editor_win, y, x);
                if (x == size.ws_row - 1 - CNTRL_ROWS_NUM) continue;
                wmove(editor_win, y + 1, x);
                wrefresh(editor_win);
                break;
            }

            case KEY_F(5): {
                char filename[MAX_PATH + 1]; // filename buffer

                // save cursor coords
                int x, y;
                getyx(editor_win, y, x);

                wmove(control_win, 0, 0);
                wclrtoeol(control_win);
                wprintw(control_win, "Enter name of file to open: ");
                wrefresh(control_win);

                echo(); // temporary turn echoing to see entered symbols
                wgetnstr(control_win, filename, MAX_PATH);
                noecho();

                filename[MAX_PATH] = '\0';

                // opening file to read
                FILE *stream;
                stream = fopen(filename, "r");
                if (!stream) {
                    wmove(control_win, 0, 0);
                    wclrtoeol(control_win);
                    wprintw(control_win, "Unable to open file \"%s\".", filename);
                    wrefresh(control_win);

                    // return cursor back
                    wmove(editor_win, y, x);
                    continue;
                }

                // TODO implement saving current content dialog

                werase(editor_win); // erase all symbols on editor screen for new file
                wmove(editor_win, 0, 0);
                int ch;
                // printing file contents
                while ((ch = fgetc(stream)) != EOF) {
                    waddch(editor_win, ch);
                }

                //close file
                if (fclose(stream) == EOF) {
                    perror("fclose");
                    return 1;
                }

                wmove(control_win, 0, 0);
                wclrtoeol(control_win);
                wprintw(control_win, "File \"%s\" succesfully opened.", filename);
                wrefresh(control_win);

                wmove(editor_win, 0, 0);
                wrefresh(editor_win);
                break;
            }

            case KEY_F(6): {
                char filename[MAX_PATH + 1]; // filename buffer

                // save cursor coords
                int x, y;
                getyx(editor_win, y, x);

                wmove(control_win, 0, 0);
                wclrtoeol(control_win);
                wprintw(control_win, "Enter name of file to save: ");
                wrefresh(control_win);

                echo(); // temporary turn echoing to see entered symbols
                wgetnstr(control_win, filename, MAX_PATH);
                noecho();

                filename[MAX_PATH] = '\0';

                // TODO check if file exists and provide override dialog

                // opening file to save
                FILE *stream;
                stream = fopen(filename, "w");
                if (!stream) {
                    wmove(control_win, 0, 0);
                    wclrtoeol(control_win);
                    wprintw(control_win, "Unable to save file \"%s\".", filename);
                    wrefresh(control_win);
                    // return cursor back
                    wmove(editor_win, y, x);
                    continue;
                }

                // save all printed in editor window symbols to file
                wmove(editor_win, 0, 0);
                char strbuf[MAX_LINE_LEN + 1];

                while (1) { // save all symbols in terminal line to string
                    winnstr(editor_win, strbuf, MAX_LINE_LEN);
                    strbuf[MAX_LINE_LEN] = '\0';
                    fputs(strbuf, stream);

                    int temp_x, temp_y;
                    getyx(editor_win, temp_y, temp_x);

                    // last line in editor window reached
                    if (temp_y == size.ws_row - 1 - CNTRL_ROWS_NUM)
                        break;

                    wmove(editor_win, temp_y + 1, 0);
                }

                //close saved file
                if (fclose(stream) == EOF) {
                    perror("fclose");
                    return 1;
                }

                wmove(control_win, 0, 0);
                wclrtoeol(control_win);
                wprintw(control_win, "File \"%s\" succesfully saved.", filename);
                wrefresh(control_win);

                // return cursor back
                wmove(editor_win, y, x);
                break;
            }

            case 'q': {
                endwin();
                exit(EXIT_SUCCESS);
            }

            default:
                if (isprint(c) || isspace(c))
                    wechochar(editor_win, c);
                break;
        }
    }
}
