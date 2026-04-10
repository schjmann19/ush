#ifndef SHELL_H
#define SHELL_H

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>

#include "builtins/builtins.h"
#include "util.h"

#define MAX_HISTORY 10000
#define USHRC ".ushrc"

struct command {
    char **argv;
    int argc;
    char *in;
    char *out;
    int append;
};

extern char *history[MAX_HISTORY];
extern int hist_count;

int is_builtin(const char *cmd);
int run_builtin(struct command *cmd, int *exit_requested);
int parse_line(char *line, struct command **cmds_out, int *ncmds_out);
void free_commands(struct command *cmds, int ncmds);
int run_pipeline(struct command *cmds, int ncmds);
char *dup_token(const char *start, size_t len);
char *expand_variables(const char *tok);
char *next_token(char **sp);
int run_builtin_with_redirection(struct command *cmd, int *exit_requested);
char *read_line(void);
void load_history(const char *file);
void save_history(const char *file);
void source_file(const char *file);

#endif /* SHELL_H */