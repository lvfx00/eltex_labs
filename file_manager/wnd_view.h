#pragma once

#include <curses.h>

void init_file_panels(WINDOW *brdr_wins[2], WINDOW *fpnl_wins[2], WINDOW **ctrl_win, int fpnl_color, int ctrl_color);

void init_copy_win(WINDOW **copy_box_win, WINDOW **copy_win, int color);

void refresh_file_list(char **file_list, size_t file_num, WINDOW *win);

void set_str_color(WINDOW *win, int str_num, int color);

void set_copying_progress(WINDOW *copy_win, int progress);
