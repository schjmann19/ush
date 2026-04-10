#include "shell.h"
#include <stdio.h>
#include <unistd.h>

char *history[MAX_HISTORY];
int hist_count = 0;

int is_builtin(const char *cmd) {
    if (!cmd) {
        return 0;
    }

    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            return 1;
        }
    }

    return 0;
}

int run_builtin(struct command *cmd, int *exit_requested) {
    if (!cmd || !cmd->argv || cmd->argc == 0) {
        return 0;
    }

    const char *name = cmd->argv[0];
    int result = 0;
    int found = 0;

    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(name, builtins[i].name) == 0) {
            result = builtins[i].func(cmd->argc, cmd->argv);
            found = 1;
            break;
        }
    }

    if (!found) {
        return 0; // not a builtin
    }

    if (result == 2) {
        *exit_requested = 1;
        return 0;
    } else if (result == 3) {
        // run external
        if (cmd->argc < 2) return 0;
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            execvp(cmd->argv[1], cmd->argv + 1);
            fprintf(stderr, "%s: %s\n", cmd->argv[1], strerror(errno));
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    } else if (result == 4 /* source magic number "4" */) {
        // source file
        if (cmd->argc < 2) return 0;
        FILE *f = fopen(cmd->argv[1], "r");
        if (!f) {
            perror(cmd->argv[1]);
            return 1;
        }
        char *line = NULL;
        size_t len = 0;
        ssize_t nread;
        while ((nread = getline(&line, &len, f)) != -1) {
            if (nread > 0 && line[nread - 1] == '\n') {
                line[nread - 1] = '\0';
            }
            char *p = line;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '\0') continue;
            struct command *sub_cmds = NULL;
            int sub_ncmds = 0;
            if (parse_line(p, &sub_cmds, &sub_ncmds) != 0) {
                free_commands(sub_cmds, sub_ncmds);
                continue;
            }
            if (sub_ncmds == 0) {
                free_commands(sub_cmds, sub_ncmds);
                continue;
            }
            int sub_exit = 0;
            if (sub_ncmds == 1 && is_builtin(sub_cmds[0].argv[0])) {
                run_builtin(&sub_cmds[0], &sub_exit);
            } else {
                run_pipeline(sub_cmds, sub_ncmds);
            }
            free_commands(sub_cmds, sub_ncmds);
            if (sub_exit) {
                *exit_requested = 1;
                break;
            }
        }
        free(line);
        fclose(f);
        return 0;
    } else {
        return result;
    }
}

char *dup_token(const char *start, size_t len) {
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

char *expand_variables(const char *tok) {
    if (!tok || tok[0] != '$' || tok[1] == '\0') {
        return strdup(tok ? tok : "");
    }

    const char *name = tok + 1;
    const char *val = getenv(name);
    return strdup(val ? val : "");
}

void source_file(const char *file) {
    if (!file) return;
    
    FILE *f = fopen(file, "r");
    if (!f) {
        return;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    
    while ((nread = getline(&line, &len, f)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;
        
        struct command *sub_cmds = NULL;
        int sub_ncmds = 0;
        
        if (parse_line(p, &sub_cmds, &sub_ncmds) != 0) {
            free_commands(sub_cmds, sub_ncmds);
            continue;
        }
        
        if (sub_ncmds == 0) {
            free_commands(sub_cmds, sub_ncmds);
            continue;
        }
        
        int sub_exit = 0;
        if (sub_ncmds == 1 && is_builtin(sub_cmds[0].argv[0])) {
            run_builtin(&sub_cmds[0], &sub_exit);
        } else {
            run_pipeline(sub_cmds, sub_ncmds);
        }
        
        free_commands(sub_cmds, sub_ncmds);
    }
    
    free(line);
    fclose(f);
}

char *next_token(char **sp) {
    if (!sp || !*sp) {
        return NULL;
    }

    char *p = *sp;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '\0') {
        *sp = p;
        return NULL;
    }

    char *start = p;

    if (*p == '|' || *p == '<' || *p == '>') {
        if (*p == '>' && *(p + 1) == '>') {
            *sp = p + 2;
            return dup_token(start, 2);
        }
        *sp = p + 1;
        return dup_token(start, 1);
    }

    /* handle quoted strings */
    int in_quote = 0;
    char quote_char = '\0';
    
    while (*p) {
        if (!in_quote && (*p == '\'' || *p == '"')) {
            in_quote = 1;
            quote_char = *p;
            p++;
            continue;
        }
        if (in_quote && *p == quote_char) {
            in_quote = 0;
            p++;
            continue;
        }
        if (!in_quote && (isspace((unsigned char)*p) || *p == '|' || *p == '<' || *p == '>')) {
            break;
        }
        p++;
    }

    size_t len = (size_t)(p - start);
    *sp = p;
    
    /* strip surrounding quotes if present */
    if (len >= 2 && ((start[0] == '\'' && start[len-1] == '\'') ||
                     (start[0] == '"' && start[len-1] == '"'))) {
        return dup_token(start + 1, len - 2);
    }
    
    return dup_token(start, len);
}

int parse_line(char *line, struct command **cmds_out, int *ncmds_out) {
    if (!line || !cmds_out || !ncmds_out) {
        return -1;
    }

    int cap = 4;
    int ncmds = 0;
    struct command *cmds = calloc((size_t)cap, sizeof(*cmds));
    if (!cmds) {
        return -1;
    }

    struct command *cur = &cmds[ncmds++];
    cur->argv = NULL;
    cur->argc = 0;
    cur->in = NULL;
    cur->out = NULL;
    cur->append = 0;

    char *sp = line;
    char *tok;

    while ((tok = next_token(&sp))) {
            char *expanded = expand_variables(tok);
            free(tok);
            if (!expanded) {
                free_commands(cmds, ncmds);
                return -1;
            }
            tok = expanded;

            if (strcmp(tok, "|") == 0) {
                free(tok);
                if (cur->argc == 0) {
                    fprintf(stderr, "syntax error near unexpected token `|`\n");
                    free_commands(cmds, ncmds);
                    return -1;
                }
                if (ncmds >= cap) {
                    int new_cap = cap * 2;
                    struct command *tmp = realloc(cmds, (size_t)new_cap * sizeof(*cmds));
                    if (!tmp) {
                        free_commands(cmds, ncmds);
                        return -1;
                    }
                    cmds = tmp;
                    cap = new_cap;
                }
                cur = &cmds[ncmds++];
                cur->argv = NULL;
                cur->argc = 0;
                cur->in = NULL;
                cur->out = NULL;
                cur->append = 0;
                continue;
            }

            if (strcmp(tok, "<") == 0) {
                free(tok);
                char *file = next_token(&sp);
                if (!file) {
                    fprintf(stderr, "syntax error: expected filename after '<'\n");
                    free_commands(cmds, ncmds);
                    return -1;
                }
                char *file_expanded = expand_variables(file);
                free(file);
                if (!file_expanded) {
                    free_commands(cmds, ncmds);
                    return -1;
                }
                cur->in = file_expanded;
                continue;
            }

            if (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0) {
                int append = (tok[1] == '>');
                free(tok);
                char *file = next_token(&sp);
                if (!file) {
                    fprintf(stderr, "syntax error: expected filename after '%s'\n", append ? ">>" : ">");
                    free_commands(cmds, ncmds);
                    return -1;
                }
                char *file_expanded = expand_variables(file);
                free(file);
                if (!file_expanded) {
                    free_commands(cmds, ncmds);
                    return -1;
                }
                cur->out = file_expanded;
                cur->append = append;
            continue;
        }

        /* argument */
        int need = cur->argc + 1;
        char **tmp = realloc(cur->argv, (size_t)(need + 1) * sizeof(*tmp));
        if (!tmp) {
            free_commands(cmds, ncmds);
            return -1;
        }
        cur->argv = tmp;
        cur->argv[cur->argc++] = tok;
        cur->argv[cur->argc] = NULL;
        
        /* expand alias on first argument */
        if (cur->argc == 1) {
            const char *alias_value = get_alias(tok);
            if (alias_value) {
                char *alias_copy = strdup(alias_value);
                if (!alias_copy) {
                    free_commands(cmds, ncmds);
                    return -1;
                }
                
                /* parse alias value as tokens */
                char *alias_sp = alias_copy;
                char *alias_tok;
                int alias_pos = 0;
                
                while ((alias_tok = next_token(&alias_sp)) && alias_pos < 100) {
                    if (alias_pos == 0) {
                        /* replace first argument with first alias token */
                        free(cur->argv[0]);
                        cur->argv[0] = alias_tok;
                    } else {
                        /* shift existing args and insert new ones */
                        int new_need = cur->argc + 1;
                        char **new_tmp = realloc(cur->argv, (size_t)(new_need + 1) * sizeof(*new_tmp));
                        if (!new_tmp) {
                            free(alias_copy);
                            free_commands(cmds, ncmds);
                            return -1;
                        }
                        cur->argv = new_tmp;
                        /* shift args to the right */
                        for (int i = cur->argc; i > alias_pos; i--) {
                            cur->argv[i] = cur->argv[i - 1];
                        }
                        cur->argv[alias_pos] = alias_tok;
                        cur->argc++;
                        cur->argv[cur->argc] = NULL;
                    }
                    alias_pos++;
                }
                
                free(alias_copy);
            }
        }
    }

    if (cur->argc == 0) {
        /* trailing pipe is error */
        free_commands(cmds, ncmds);
        fprintf(stderr, "syntax error: unexpected end of line\n");
        return -1;
    }

    *cmds_out = cmds;
    *ncmds_out = ncmds;
    return 0;
}

void free_commands(struct command *cmds, int ncmds) {
    if (!cmds) {
        return;
    }
    for (int i = 0; i < ncmds; i++) {
        if (cmds[i].argv) {
            for (int j = 0; cmds[i].argv[j]; j++) {
                free(cmds[i].argv[j]);
            }
            free(cmds[i].argv);
        }
        free(cmds[i].in);
        free(cmds[i].out);
    }
    free(cmds);
}

int run_pipeline(struct command *cmds, int ncmds) {
    if (ncmds <= 0) { return 0; }

    int prev_fd = -1;

    for (int i = 0; i < ncmds; i++) {
        int pipe_fd[2] = {-1, -1};
        if (i < ncmds - 1) {
            if (pipe(pipe_fd) != 0) {
                perror("pipe");
                return -1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (prev_fd != -1) {
                close(prev_fd);
            }
            if (pipe_fd[0] != -1) {
                close(pipe_fd[0]);
                close(pipe_fd[1]);
            }
            return -1;
        }

        if (pid == 0) {
            /* child */
            signal(SIGINT, SIG_DFL);

            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }

            if (pipe_fd[1] != -1) {
                dup2(pipe_fd[1], STDOUT_FILENO);
                close(pipe_fd[0]);
                close(pipe_fd[1]);
            }

            if (cmds[i].in) {
                int fd = open(cmds[i].in, O_RDONLY);
                if (fd < 0) {
                    perror(cmds[i].in);
                    _exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (cmds[i].out) {
                int flags = O_WRONLY | O_CREAT;
                if (cmds[i].append) {
                    flags |= O_APPEND;
                } else {
                    flags |= O_TRUNC;
                }
                int fd = open(cmds[i].out, flags, 0666);
                if (fd < 0) {
                    perror(cmds[i].out);
                    _exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            if (is_builtin(cmds[i].argv[0])) {
                int dummy_exit = 0;
                run_builtin(&cmds[i], &dummy_exit);
                _exit(0);
            }

            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "%s: %s\n", cmds[i].argv[0],
                    strerror(errno));
            _exit(127);
        } 

        /* parent */
        if (prev_fd != -1) {
            close(prev_fd);
        }
        if (pipe_fd[1] != -1) {
            close(pipe_fd[1]);
        }
        prev_fd = pipe_fd[0];
    }

    int status = 0;
    while (wait(&status) > 0) {
        ;
    }

    return 0;
}

int run_builtin_with_redirection(struct command *cmd, int *exit_requested) {
    int saved_stdin = -1;
    int saved_stdout = -1;
    int in_fd = -1;
    int out_fd = -1;

    if (cmd->in) {
        in_fd = open(cmd->in, O_RDONLY);
        if (in_fd < 0) {
            perror(cmd->in);
            return 1;
        }
        saved_stdin = dup(STDIN_FILENO);
        if (saved_stdin < 0) {
            perror("dup");
            close(in_fd);
            return 1;
        }
        if (dup2(in_fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(in_fd);
            close(saved_stdin);
            return 1;
        }
        close(in_fd);
    }

    if (cmd->out) {
        int flags = O_WRONLY | O_CREAT;
        if (cmd->append) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }
        out_fd = open(cmd->out, flags, 0666);
        if (out_fd < 0) {
            perror(cmd->out);
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            return 1;
        }
        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdout < 0) {
            perror("dup");
            close(out_fd);
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            return 1;
        }
        if (dup2(out_fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(out_fd);
            close(saved_stdout);
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            return 1;
        }
        close(out_fd);
    }

    int result = run_builtin(cmd, exit_requested);

    if (saved_stdin >= 0) {
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
    }
    if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }

    return result;
}

char *read_line(void) {
    if (!isatty(STDIN_FILENO)) {
        char *line = NULL;
        size_t len = 0;
        ssize_t n = getline(&line, &len, stdin);
        if (n == -1) return NULL;
        if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';
        return line;
    }

    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    char buffer[1024] = {0};
    int pos = 0;
    int hist_index = hist_count;

    fputs("$ ", stdout);
    fflush(stdout);

    loop {
        int c = getchar();
        if (c == EOF) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
            return NULL;
        }
        if (c == '\n' || c == '\r') {
            buffer[pos] = '\0';
            putchar('\n');
            tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
            char *line = strdup(buffer);
            if (line && *line && hist_count < MAX_HISTORY) {
                history[hist_count++] = strdup(line);
            }
            return line;
        } else if (c == 127 || c == 8) { // backspace
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (c == 27) { // escape sequence
            if (getchar() == '[') {
                int arrow = getchar();
                if (arrow == 'A') { // up
                    if (hist_index > 0) {
                        hist_index--;
                        // clear current line
                        for (int i = 0; i < pos; i++) printf("\b \b");
                        strcpy(buffer, history[hist_index]);
                        pos = strlen(buffer);
                        printf("%s", buffer);
                        fflush(stdout);
                    }
                } else if (arrow == 'B') { // down
                    if (hist_index < hist_count) {
                        hist_index++;
                        for (int i = 0; i < pos; i++) printf("\b \b");
                        if (hist_index < hist_count) {
                            strcpy(buffer, history[hist_index]);
                            pos = strlen(buffer);
                            printf("%s", buffer);
                        } else {
                            pos = 0;
                            buffer[0] = '\0';
                        }
                        fflush(stdout);
                    }
                }
            }
        } else if (c >= 32 && c <= 126 && pos < 1023) {
            buffer[pos++] = c;
            putchar(c);
            fflush(stdout);
        }
    }
}

void load_history(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f) && hist_count < MAX_HISTORY) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (*line) {
            history[hist_count++] = strdup(line);
        }
    }
    fclose(f);
}

void save_history(const char *file) {
    FILE *f = fopen(file, "w");
    if (!f) return;
    for (int i = 0; i < hist_count; i++) {
        fprintf(f, "%s\n", history[i]);
        free(history[i]);
    }
    fclose(f);
}