#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>

#define MAX_INPUT_LEN 512

#define MAX_CMD_LEN 128
#define MAX_CMD_NUM 16

int main(int argc, char **argv) {
    char *cmds[MAX_CMD_NUM];
    int cmd_num = 0;

    char cur_cmd_buf[MAX_CMD_LEN];
    // empty string
    cur_cmd_buf[0] = '\0';

    char cmd_part_buf[MAX_CMD_LEN];

    char input_str[MAX_INPUT_LEN + 1];

    printf("Enter command: ");
    fgets(input_str, MAX_INPUT_LEN + 1, stdin);
    // remove newline character
    size_t len = strlen(input_str);
    if (input_str[len - 1] == '\n')
        input_str[len - 1] = '\0';

    char *cur_pos = input_str;
    char *next_space;
    while ((next_space = strchr(cur_pos, ' ')) != NULL) {

        *next_space = '\0';
        strcpy(cmd_part_buf, cur_pos);
        cur_pos = next_space + 1;

        if (strcmp(cmd_part_buf, "|") == 0) {
            // TODO add command filter
            // save command in buffer
            if (cur_cmd_buf[0] != '\0') {
                size_t len = strlen(cur_cmd_buf);
                cmds[cmd_num] = malloc(len + 1);
                if (!cmds[cmd_num]) {
                    perror("malloc");
                    exit(EXIT_FAILURE);
                }
                memcpy(cmds[cmd_num], cur_cmd_buf, len + 1);
                cmd_num++;
                cur_cmd_buf[0] = '\0';
            }
        } else {
            // add word to current command
            if (cur_cmd_buf[0] != '\0')
                strcat(cur_cmd_buf, " ");
            strcat(cur_cmd_buf, cmd_part_buf);
        }
    }

    if (*cur_pos != '\0') {
        strcpy(cmd_part_buf, cur_pos);
        if (strcmp(cmd_part_buf, "|") != 0) {
            if (cur_cmd_buf[0] != '\0')
                strcat(cur_cmd_buf, " ");
            strcat(cur_cmd_buf, cmd_part_buf);
        }
    }

    if (cur_cmd_buf[0] != '\0') {
        cmds[cmd_num] = malloc(strlen(cur_cmd_buf) + 1);
        if (!cmds[cmd_num]) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        memcpy(cmds[cmd_num], cur_cmd_buf, len + 1);
        cmd_num++;
    }

    if (cmd_num > 1) {
        // run first cmd and refer its stdout to pipe
        int filedes[2];
        if (pipe(filedes) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
        switch (fork()) {
            case 0: { // child process
                if (close(filedes[0]) == -1) { // close unused read pipe end
                    perror("close");
                    exit(EXIT_FAILURE);
                }
                // refer stdout to the pipe end
                if (filedes[1] != STDOUT_FILENO) {
                    if (dup2(filedes[1], STDOUT_FILENO) == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    if (close(filedes[1]) == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                }
                // exec cmd
                execl("/bin/sh", "sh", "-c", cmds[0], (char *) 0);
                perror("execl");
                exit(EXIT_FAILURE);
            }

            case -1:
                perror("fork");
                exit(EXIT_FAILURE);

            default: // parent process
                if (close(filedes[1]) == -1) { // close write pipe end
                    perror("close");
                    exit(EXIT_FAILURE);
                }
                break;
        }

        // run processes that use both pipe ends
        for (int i = 1; i < cmd_num - 1; ++i) {
            // create next pipe
            int next_filedes[2];
            if (pipe(next_filedes) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
            switch (fork()) {
                case 0: // child process
                    // refer stdin to the previous pipe read end
                    if (filedes[0] != STDIN_FILENO) {
                        if (dup2(filedes[0], STDIN_FILENO) == -1) {
                            perror("dup2");
                            exit(EXIT_FAILURE);
                        }
                        if (close(filedes[0]) == -1) {
                            perror("close");
                            exit(EXIT_FAILURE);
                        }
                    }
                    // refer stdout to the next pipe write end
                    if (next_filedes[1] != STDOUT_FILENO) {
                        if (dup2(next_filedes[1], STDOUT_FILENO) == -1) {
                            perror("dup2");
                            exit(EXIT_FAILURE);
                        }
                        if (close(next_filedes[1]) == -1) {
                            perror("close");
                            exit(EXIT_FAILURE);
                        }
                    }
                    if (close(next_filedes[0]) == -1) { // close unused read end of next pipe
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    // write end of previous pipe was already closed in parent process

                    // exec cmd
                    execl("/bin/sh", "sh", "-c", cmds[i], (char *) 0);
                    perror("execl");
                    exit(EXIT_FAILURE);

                case -1:
                    perror("fork");
                    exit(EXIT_FAILURE);

                default: // parent process
                    if (close(next_filedes[1]) == -1) { // close write end of next pipe
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    if (close(filedes[0]) == -1) { // close read end of previous pipe
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                    // save file descriptor of next pipe read end to previous array
                    filedes[0] = next_filedes[0];
                    break;
            }
        }

        // run last cmd and refer its stdin to last pipe read end
        switch (fork()) {
            case 0: { // child process
                // refer stdin to the last pipe read end
                if (filedes[0] != STDIN_FILENO) {
                    if (dup2(filedes[0], STDIN_FILENO) == -1) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    if (close(filedes[0]) == -1) {
                        perror("close");
                        exit(EXIT_FAILURE);
                    }
                }

                // exec cmd
                execl("/bin/sh", "sh", "-c", cmds[cmd_num - 1], (char *) 0);
                perror("execl");
                exit(EXIT_FAILURE);
            }

            case -1:
                perror("fork");
                exit(EXIT_FAILURE);

            default: // parent process
                if (close(filedes[0]) == -1) { // close read end of last pipe
                    perror("close");
                    exit(EXIT_FAILURE);
                }
                break;
        }

        // wait child processes
        for(int i = 0; i < cmd_num; ++i) {
            if(wait(0) == -1) {
                perror("wait");
                exit(EXIT_FAILURE);
            }
        }

    } else if (cmd_num == 1) {
        // just run specified cmd
        execl("/bin/sh", "sh", "-c", cmds[0], (char *) 0);
        perror("execl");
        exit(EXIT_FAILURE);

    } else {
        printf("usage: command | command | ...\n");
    }

    exit(EXIT_SUCCESS);
}