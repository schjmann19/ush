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

enum command_type {
    CMD_SIMPLE,
    CMD_IF,
    CMD_FOR,
    CMD_CASE,
    CMD_FUNCTION_DEF,
    CMD_WHILE,
    CMD_UNTIL,
};

struct case_item {
    char *pattern;
    struct command *cmds;
    int ncmds;
};

struct command {
    enum command_type type;
    char **argv;
    int argc;
    char *in;
    char *out;
    int append;

    /* compound command data */
    struct command *cond_cmds;
    int cond_ncmds;
    struct command *then_cmds;
    int then_ncmds;
    struct command *else_cmds;
    int else_ncmds;

    char *for_var;
    char **for_values;
    int for_nvalues;
    struct command *for_body;
    int for_body_ncmds;
    char *for_body_line;

    char *case_word;
    struct case_item *case_items;
    int case_item_count;

    char *func_name;
    struct command *func_body;
    int func_ncmds;
    
    /* while/until loop condition and body */
    struct command *loop_cond_cmds;
    int loop_cond_ncmds;
    char *loop_cond_line;
    struct command *loop_body_cmds;
    int loop_body_ncmds;
    char *loop_body_line;
    
    /* Logical operators: 0=none, 1=&&, 2=|| */
    int op_after;
};

extern char *history[MAX_HISTORY];
extern int hist_count;

int is_builtin(const char *cmd);
int run_builtin(struct command *cmd, int *exit_requested);
int parse_line(char *line, struct command **cmds_out, int *ncmds_out);
void free_commands(struct command *cmds, int ncmds);
int run_pipeline(struct command *cmds, int ncmds);
int execute_commands(struct command *cmds, int ncmds, int *exit_requested);
char *dup_token(const char *start, size_t len);
char *expand_variables(const char *tok);
char *next_token(char **sp);
int run_builtin_with_redirection(struct command *cmd, int *exit_requested);
char *read_line(void);
void load_history(const char *file);
void save_history(const char *file);
void source_file(const char *file);
void init_shell_special_vars(pid_t pid);
void set_positional_args(char **argv, int argc);

#endif /* SHELL_H */