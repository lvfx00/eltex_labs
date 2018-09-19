#pragma once

#include <curses.h>

#define CHAT_WND_COLOR 1
#define INPUT_WND_COLOR 2
#define CTRL_WND_COLOR 3

#define INPUT_WND_HEIGHT 6
#define CTRL_WND_HEIGHT 3

#define ALT_KEY_BACKSPACE 127
#define ALT_KEY_ENTER 10

#define UPDATE_INTERVAL 500000000 // in nanosecs

void *input_handler(void *arg);

void init_file_panels(WINDOW **chat_brdr_wnd, WINDOW **chat_wnd, WINDOW **input_wnd, WINDOW **ctrl_wnd);

void exit_with_msg(WINDOW *wnd, char *msg);
