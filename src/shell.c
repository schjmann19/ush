#include "shell.h"
#include <stdio.h>
#include <unistd.h>

char *history[MAX_HISTORY];
int hist_count = 0;

/* Special variables for POSIX compliance */
static int last_exit_status = 0;
static pid_t shell_pid = 0;
static char **positional_args = NULL;
static int positional_args_count = 0;

char *dup_token(const char *start, size_t len) {
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

struct shell_function {
    char *name;
    struct command *body_cmds;
    int body_ncmds;
};

static struct shell_function *shell_functions = NULL;
static int shell_function_count = 0;

static struct shell_function *find_function(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < shell_function_count; i++) {
        if (strcmp(shell_functions[i].name, name) == 0) {
            return &shell_functions[i];
        }
    }
    return NULL;
}

static void free_function(struct shell_function *func) {
    if (!func) return;
    free(func->name);
    if (func->body_cmds) {
        free_commands(func->body_cmds, func->body_ncmds);
    }
    func->name = NULL;
    func->body_cmds = NULL;
    func->body_ncmds = 0;
}

static int define_function(const char *name, struct command *body_cmds, int body_ncmds) {
    if (!name) {
        free_commands(body_cmds, body_ncmds);
        return -1;
    }

    struct shell_function *existing = find_function(name);
    if (existing) {
        free_function(existing);
        existing->name = strdup(name);
        if (!existing->name) {
            free_commands(body_cmds, body_ncmds);
            return -1;
        }
        existing->body_cmds = body_cmds;
        existing->body_ncmds = body_ncmds;
        return 0;
    }

    struct shell_function *new_functions = realloc(shell_functions,
        (size_t)(shell_function_count + 1) * sizeof(*new_functions));
    if (!new_functions) {
        free_commands(body_cmds, body_ncmds);
        return -1;
    }

    shell_functions = new_functions;
    shell_functions[shell_function_count].name = strdup(name);
    if (!shell_functions[shell_function_count].name) {
        free_commands(body_cmds, body_ncmds);
        return -1;
    }
    shell_functions[shell_function_count].body_cmds = body_cmds;
    shell_functions[shell_function_count].body_ncmds = body_ncmds;
    shell_function_count++;
    return 0;
}

static int is_shell_function(const char *cmd) {
    return find_function(cmd) != NULL;
}

static int run_shell_function(const char *name) {
    struct shell_function *func = find_function(name);
    if (!func) return 0;
    if (func->body_ncmds == 0) return 0;
    return execute_commands(func->body_cmds, func->body_ncmds, NULL);
}

static int match_pattern(const char *pattern, const char *text) {
    if (!pattern || !text) return 0;
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return 1;
            while (*text) {
                if (match_pattern(pattern, text)) return 1;
                text++;
            }
            return 0;
        }
        if (*pattern == '?') {
            if (*text == '\0') return 0;
            pattern++;
            text++;
            continue;
        }
        if (*pattern != *text) return 0;
        pattern++;
        text++;
    }
    return *text == '\0';
}

static char *join_tokens(char **tokens, int start, int end) {
    size_t len = 0;
    for (int i = start; i < end; i++) {
        if (strcmp(tokens[i], ";") == 0) {
            continue;
        }
        len += strlen(tokens[i]) + 1;
    }

    char *out = malloc(len + 1);
    if (!out) return NULL;

    char *p = out;
    for (int i = start; i < end; i++) {
        if (strcmp(tokens[i], ";") == 0) {
            continue;
        }
        size_t tlen = strlen(tokens[i]);
        memcpy(p, tokens[i], tlen);
        p += tlen;
        if (i + 1 < end) {
            *p++ = ' ';
        }
    }
    if (p > out && p[-1] == ' ') {
        p--;
    }
    *p = '\0';
    return out;
}

/* Expand ${VAR} style parameter expansion */
static char *expand_param_expansion(const char *str) {
    if (!str || str[0] != '$' || str[1] != '{') {
        return strdup(str ? str : "");
    }

    const char *p = str + 2;
    const char *end = strchr(p, '}');
    if (!end) {
        return strdup(str);
    }

    size_t param_len = (size_t)(end - p);
    char param_str[256];
    if (param_len >= sizeof(param_str)) {
        return strdup(str);
    }
    memcpy(param_str, p, param_len);
    param_str[param_len] = '\0';

    /* Handle ${#VAR} - length expansion */
    if (param_str[0] == '#') {
        const char *var_name = param_str + 1;
        const char *val = getenv(var_name);
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", val ? strlen(val) : 0);
        
        char *result = malloc(strlen(buf) + strlen(end + 1) + 1);
        if (result) {
            strcpy(result, buf);
            strcat(result, end + 1);
        }
        return result ? result : strdup("");
    }

    /* Handle ${VAR:mode} expansions */
    const char *colon_pos = strchr(param_str, ':');
    if (colon_pos) {
        size_t var_name_len = (size_t)(colon_pos - param_str);
        char var_name[256];
        memcpy(var_name, param_str, var_name_len);
        var_name[var_name_len] = '\0';

        const char *expansion_part = colon_pos + 1;
        const char *val = getenv(var_name);
        int is_empty = !val || *val == '\0';

        /* ${VAR:-default} - use default if empty/unset */
        if (*expansion_part == '-') {
            if (is_empty) {
                char *result = malloc(strlen(expansion_part) + strlen(end + 1) + 1);
                if (result) {
                    strcpy(result, expansion_part + 1);
                    strcat(result, end + 1);
                }
                return result ? result : strdup("");
            }
            char *result = malloc(strlen(val) + strlen(end + 1) + 1);
            if (result) {
                strcpy(result, val);
                strcat(result, end + 1);
            }
            return result ? result : strdup("");
        }

        /* ${VAR:+alternate} - use alternate if set/non-empty */
        if (*expansion_part == '+') {
            if (!is_empty) {
                char *result = malloc(strlen(expansion_part) + strlen(end + 1) + 1);
                if (result) {
                    strcpy(result, expansion_part + 1);
                    strcat(result, end + 1);
                }
                return result ? result : strdup("");
            }
            return strdup(end + 1);
        }

        /* ${VAR:=default} - assign default if empty/unset */
        if (*expansion_part == '=') {
            if (is_empty) {
                setenv(var_name, expansion_part + 1, 1);
                char *result = malloc(strlen(expansion_part) + strlen(end + 1) + 1);
                if (result) {
                    strcpy(result, expansion_part + 1);
                    strcat(result, end + 1);
                }
                return result ? result : strdup("");
            }
            char *result = malloc(strlen(val) + strlen(end + 1) + 1);
            if (result) {
                strcpy(result, val);
                strcat(result, end + 1);
            }
            return result ? result : strdup("");
        }

        /* ${VAR:?error} - show error if unset */
        if (*expansion_part == '?') {
            if (is_empty) {
                fprintf(stderr, "parameter expansion error: %s\n", expansion_part + 1);
                return strdup("");
            }
            char *result = malloc(strlen(val) + strlen(end + 1) + 1);
            if (result) {
                strcpy(result, val);
                strcat(result, end + 1);
            }
            return result ? result : strdup("");
        }
    }

    /* Simple ${VAR} expansion */
    const char *val = getenv(param_str);
    char *result = malloc((val ? strlen(val) : 0) + strlen(end + 1) + 1);
    if (result) {
        if (val) {
            strcpy(result, val);
        } else {
            result[0] = '\0';
        }
        strcat(result, end + 1);
    }
    return result ? result : strdup("");
}

/* Command substitution support */
static char *execute_command_capture(const char *cmd_str) {
    if (!cmd_str || *cmd_str == '\0') {
        return strdup("");
    }

    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) {
        return strdup("");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return strdup("");
    }

    if (pid == 0) {
        /* Child process: redirect stdout to pipe and execute command */
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);
        signal(SIGINT, SIG_DFL);

        struct command *cmds = NULL;
        int ncmds = 0;
        char *cmd_copy = strdup(cmd_str);
        if (!cmd_copy) {
            _exit(1);
        }

        if (parse_line(cmd_copy, &cmds, &ncmds) == 0 && ncmds > 0) {
            int dummy_exit = 0;
            execute_commands(cmds, ncmds, &dummy_exit);
            free_commands(cmds, ncmds);
        }
        free(cmd_copy);
        _exit(0);
    }

    /* Parent process: read from pipe */
    close(pipe_fd[1]);

    /* Read all output from the command */
    size_t cap = 256;
    size_t len = 0;
    char *result = malloc(cap);
    if (!result) {
        close(pipe_fd[0]);
        waitpid(pid, NULL, 0);
        return strdup("");
    }

    ssize_t nread;
    char buf[256];
    while ((nread = read(pipe_fd[0], buf, sizeof(buf))) > 0) {
        if (len + (size_t)nread >= cap) {
            cap *= 2;
            char *tmp = realloc(result, cap);
            if (!tmp) {
                close(pipe_fd[0]);
                waitpid(pid, NULL, 0);
                free(result);
                return strdup("");
            }
            result = tmp;
        }
        memcpy(result + len, buf, (size_t)nread);
        len += (size_t)nread;
    }

    close(pipe_fd[0]);
    waitpid(pid, NULL, 0);

    result[len] = '\0';

    /* Remove trailing newlines from command output */
    while (len > 0 && result[len - 1] == '\n') {
        result[len - 1] = '\0';
        len--;
    }

    return result;
}

/* Expand command substitutions in a string */
static char *expand_command_substitution(const char *str) {
    if (!str) return strdup("");

    size_t result_cap = 256;
    size_t result_len = 0;
    char *result = malloc(result_cap);
    if (!result) return strdup("");

    const char *p = str;
    while (*p) {
        /* Handle backtick substitution: `cmd` */
        if (*p == '`') {
            p++;
            const char *cmd_start = p;
            size_t cmd_len = 0;
            while (*p && *p != '`') {
                cmd_len++;
                p++;
            }
            if (*p == '`') {
                p++;
                char *cmd = dup_token(cmd_start, cmd_len);
                char *output = execute_command_capture(cmd);
                free(cmd);

                size_t out_len = strlen(output);
                while (result_len + out_len >= result_cap) {
                    result_cap *= 2;
                    char *tmp = realloc(result, result_cap);
                    if (!tmp) {
                        free(result);
                        free(output);
                        return strdup("");
                    }
                    result = tmp;
                }
                memcpy(result + result_len, output, out_len);
                result_len += out_len;
                free(output);
                continue;
            }
        }

        /* Handle $(...) substitution */
        if (*p == '$' && *(p + 1) == '(') {
            p += 2;
            const char *cmd_start = p;
            size_t cmd_len = 0;
            int paren_depth = 1;
            while (*p && paren_depth > 0) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') paren_depth--;
                if (paren_depth > 0) {
                    cmd_len++;
                }
                p++;
            }
            if (paren_depth == 0) {
                char *cmd = dup_token(cmd_start, cmd_len);
                char *output = execute_command_capture(cmd);
                free(cmd);

                size_t out_len = strlen(output);
                while (result_len + out_len >= result_cap) {
                    result_cap *= 2;
                    char *tmp = realloc(result, result_cap);
                    if (!tmp) {
                        free(result);
                        free(output);
                        return strdup("");
                    }
                    result = tmp;
                }
                memcpy(result + result_len, output, out_len);
                result_len += out_len;
                free(output);
                continue;
            }
        }

        /* Regular character */
        if (result_len + 1 >= result_cap) {
            result_cap *= 2;
            char *tmp = realloc(result, result_cap);
            if (!tmp) {
                free(result);
                return strdup("");
            }
            result = tmp;
        }
        result[result_len++] = *p++;
    }

    result[result_len] = '\0';
    return result;
}

char *expand_variables(const char *tok) {
    if (!tok) {
        return strdup("");
    }

    char *result = NULL;

    /* Handle ${...} parameter expansion first */
    if (strstr(tok, "${") != NULL) {
        result = strdup(tok);
        if (!result) return strdup("");
        
        /* Process all ${...} expansions */
        int changed = 1;
        while (changed) {
            changed = 0;
            const char *start = strstr(result, "${");
            if (start) {
                const char *end = strchr(start, '}');
                if (end) {
                    /* Extract and expand this parameter */
                    char *expanded = expand_param_expansion(start);
                    
                    /* Build new result with expanded part */
                    size_t prefix_len = (size_t)(start - result);
                    size_t suffix_len = strlen(end + 1);
                    size_t expanded_len = strlen(expanded);
                    
                    char *new_result = malloc(prefix_len + expanded_len + suffix_len + 1);
                    if (!new_result) {
                        free(result);
                        free(expanded);
                        return strdup("");
                    }
                    
                    memcpy(new_result, result, prefix_len);
                    memcpy(new_result + prefix_len, expanded, expanded_len);
                    strcpy(new_result + prefix_len + expanded_len, end + 1);
                    
                    free(result);
                    free(expanded);
                    result = new_result;
                    changed = 1;
                }
            }
        }
        
        /* Continue with command substitution and variable expansion on result */
        char *after_cmdsub = expand_command_substitution(result);
        free(result);
        
        /* If no variables, return the result */
        if (strchr(after_cmdsub, '$') == NULL) {
            return after_cmdsub;
        }
        
        result = strdup(after_cmdsub);
        free(after_cmdsub);
    } else {
        /* First, handle command substitution in the whole token */
        char *after_cmdsub = expand_command_substitution(tok);
        
        /* If no variables, return the result */
        if (strchr(after_cmdsub, '$') == NULL) {
            return after_cmdsub;
        }
        
        result = strdup(after_cmdsub);
        free(after_cmdsub);
    }

    /* Now handle variable expansion */
    size_t result_cap = 4096;
    char *final_result = malloc(result_cap);
    if (!final_result) {
        free(result);
        return strdup("");
    }
    
    size_t final_len = 0;
    const char *p = result;
    
    while (*p && final_len < result_cap - 1) {
        if (*p == '$' && *(p + 1) != '\0' && *(p + 1) != '{') {
            p++;
            
            /* Handle special variables: $?, $$, $#, $@, $* */
            if (*p == '?') {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", last_exit_status);
                size_t len = strlen(buf);
                if (final_len + len < result_cap) {
                    memcpy(final_result + final_len, buf, len);
                    final_len += len;
                }
                p++;
                continue;
            }
            if (*p == '$') {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", (int)shell_pid);
                size_t len = strlen(buf);
                if (final_len + len < result_cap) {
                    memcpy(final_result + final_len, buf, len);
                    final_len += len;
                }
                p++;
                continue;
            }
            if (*p == '#') {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", positional_args_count);
                size_t len = strlen(buf);
                if (final_len + len < result_cap) {
                    memcpy(final_result + final_len, buf, len);
                    final_len += len;
                }
                p++;
                continue;
            }
            if (*p == '@' || *p == '*') {
                /* $@ and $* expand to all positional arguments */
                for (int i = 0; i < positional_args_count; i++) {
                    if (positional_args[i]) {
                        size_t len = strlen(positional_args[i]);
                        if (final_len + len < result_cap) {
                            memcpy(final_result + final_len, positional_args[i], len);
                            final_len += len;
                        }
                        if (i < positional_args_count - 1 && final_len < result_cap - 1) {
                            final_result[final_len++] = ' ';
                        }
                    }
                }
                p++;
                continue;
            }
            
            /* Handle numeric positional parameters and regular variables */
            if (isdigit((unsigned char)*p)) {
                int idx = *p - '0';
                p++;
                if (idx < positional_args_count && positional_args[idx]) {
                    size_t len = strlen(positional_args[idx]);
                    if (final_len + len < result_cap) {
                        memcpy(final_result + final_len, positional_args[idx], len);
                        final_len += len;
                    }
                }
                continue;
            }
            
            if (isalpha((unsigned char)*p) || *p == '_') {
                const char *var_start = p;
                while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
                    p++;
                }
                size_t var_len = (size_t)(p - var_start);
                char var_name[256];
                if (var_len < sizeof(var_name)) {
                    memcpy(var_name, var_start, var_len);
                    var_name[var_len] = '\0';
                    
                    const char *val = getenv(var_name);
                    if (val) {
                        size_t len = strlen(val);
                        if (final_len + len < result_cap) {
                            memcpy(final_result + final_len, val, len);
                            final_len += len;
                        }
                    }
                }
                continue;
            }
        }
        
        final_result[final_len++] = *p++;
    }
    
    final_result[final_len] = '\0';
    free(result);
    return final_result;
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
        execute_commands(sub_cmds, sub_ncmds, &sub_exit);
        free_commands(sub_cmds, sub_ncmds);
        if (sub_exit) {
            break;
        }
    }

    free(line);
    fclose(f);
}

void init_shell_special_vars(pid_t pid) {
    shell_pid = pid;
    if (!positional_args) {
        positional_args = calloc(1, sizeof(char *));
        positional_args_count = 0;
    }
}

void set_positional_args(char **argv, int argc) {
    /* Free old arguments */
    if (positional_args) {
        for (int i = 0; i < positional_args_count; i++) {
            free(positional_args[i]);
        }
        free(positional_args);
    }

    /* Set new arguments */
    positional_args_count = argc;
    positional_args = calloc((size_t)(argc + 1), sizeof(char *));
    if (!positional_args) {
        positional_args_count = 0;
        return;
    }

    for (int i = 0; i < argc; i++) {
        positional_args[i] = strdup(argv[i] ? argv[i] : "");
    }
    positional_args[argc] = NULL;
}

char *next_token(char **sp) {
    if (!sp || !*sp) {
        return NULL;
    }

    char *p = *sp;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '#') {
        while (*p && *p != '\n') {
            p++;
        }
        *sp = p;
        return NULL;
    }

    if (*p == '\0') {
        *sp = p;
        return NULL;
    }

    char *start = p;
    if (*p == '|' || *p == '<' || *p == '>' || *p == ';' || *p == '(' || *p == ')' || *p == '{' || *p == '}' || *p == '&') {
        if (*p == '>' && *(p + 1) == '>') {
            *sp = p + 2;
            return dup_token(start, 2);
        }
        if (*p == ';' && *(p + 1) == ';') {
            *sp = p + 2;
            return dup_token(start, 2);
        }
        if (*p == '&' && *(p + 1) == '&') {
            *sp = p + 2;
            return dup_token(start, 2);
        }
        if (*p == '|' && *(p + 1) == '|') {
            *sp = p + 2;
            return dup_token(start, 2);
        }
        *sp = p + 1;
        return dup_token(start, 1);
    }

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
        
        /* Handle ${...} parameter expansion: don't split inside it */
        if (!in_quote && *p == '$' && *(p + 1) == '{') {
            p += 2;
            int brace_depth = 1;
            while (*p && brace_depth > 0) {
                if (*p == '{') brace_depth++;
                else if (*p == '}') brace_depth--;
                p++;
            }
            continue;
        }
        
        /* Handle command substitution: don't split inside $(...) */
        if (!in_quote && *p == '$' && *(p + 1) == '(') {
            p += 2;
            int paren_depth = 1;
            while (*p && paren_depth > 0) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') paren_depth--;
                p++;
            }
            continue;
        }
        
        /* Handle backtick command substitution: don't split inside backticks */
        if (!in_quote && *p == '`') {
            p++;
            while (*p && *p != '`') {
                if (*p == '\\') p++;
                if (*p) p++;
            }
            if (*p == '`') p++;
            continue;
        }
        
        if (!in_quote && (isspace((unsigned char)*p) || *p == '|' || *p == '<' || *p == '>' || *p == ';' || *p == '(' || *p == ')' || *p == '{' || *p == '}' || *p == '&')) {
            break;
        }
        p++;
    }

    size_t len = (size_t)(p - start);
    *sp = p;

    if (len >= 2 && ((start[0] == '\'' && start[len-1] == '\'') ||
                     (start[0] == '"' && start[len-1] == '"'))) {
        return dup_token(start + 1, len - 2);
    }

    return dup_token(start, len);
}

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
        return 0;
    }

    if (result == 2) {
        *exit_requested = 1;
        return 0;
    } else if (result == 3) {
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
    } else if (result == 4) {
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
            execute_commands(sub_cmds, sub_ncmds, &sub_exit);
            free_commands(sub_cmds, sub_ncmds);
            if (sub_exit) {
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

static int tokenize_line(char *line, char ***tokens_out, int *ntokens_out) {
    int cap = 8;
    int n = 0;
    char **tokens = calloc((size_t)cap, sizeof(*tokens));
    if (!tokens) return -1;

    char *sp = line;
    char *tok;
    while ((tok = next_token(&sp))) {
        if (n >= cap) {
            int new_cap = cap * 2;
            char **tmp = realloc(tokens, (size_t)new_cap * sizeof(*tmp));
            if (!tmp) {
                for (int i = 0; i < n; i++) free(tokens[i]);
                free(tokens);
                free(tok);
                return -1;
            }
            tokens = tmp;
            cap = new_cap;
        }
        tokens[n++] = tok;
    }

    *tokens_out = tokens;
    *ntokens_out = n;
    return 0;
}

static void free_tokens(char **tokens, int ntokens) {
    if (!tokens) return;
    for (int i = 0; i < ntokens; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

static int parse_line_simple(char *line, struct command **cmds_out, int *ncmds_out);

static int parse_if_tokens(char **tokens, int ntokens, struct command **cmds_out, int *ncmds_out) {
    if (ntokens < 4 || (strcmp(tokens[0], "if") != 0 && strcmp(tokens[0], "elif") != 0)) {
        fprintf(stderr, "syntax error: expected if/elif\n");
        return -1;
    }

    int then_pos = -1;
    for (int i = 1; i < ntokens; i++) {
        if (strcmp(tokens[i], "then") == 0) {
            then_pos = i;
            break;
        }
    }
    if (then_pos < 0) {
        fprintf(stderr, "syntax error: expected 'then'\n");
        return -1;
    }

    int branch_end = -1;
    int branch_type = 0; // 0=fi, 1=else, 2=elif
    for (int i = then_pos + 1; i < ntokens; i++) {
        if (strcmp(tokens[i], "elif") == 0) {
            branch_end = i;
            branch_type = 2;
            break;
        }
        if (strcmp(tokens[i], "else") == 0) {
            branch_end = i;
            branch_type = 1;
            break;
        }
        if (strcmp(tokens[i], "fi") == 0) {
            branch_end = i;
            branch_type = 0;
            break;
        }
    }
    if (branch_end < 0) {
        fprintf(stderr, "syntax error: expected 'elif', 'else' or 'fi'\n");
        return -1;
    }

    char *cond_line = join_tokens(tokens, 1, then_pos);
    char *then_line = join_tokens(tokens, then_pos + 1, branch_end);
    char *else_line = NULL;
    if (branch_type == 1) {
        int fi_pos = -1;
        for (int i = branch_end + 1; i < ntokens; i++) {
            if (strcmp(tokens[i], "fi") == 0) {
                fi_pos = i;
                break;
            }
        }
        if (fi_pos < 0) {
            fprintf(stderr, "syntax error: expected 'fi'\n");
            free(cond_line);
            free(then_line);
            return -1;
        }
        else_line = join_tokens(tokens, branch_end + 1, fi_pos);
    }

    if (!cond_line || !then_line || (branch_type == 1 && !else_line)) {
        free(cond_line);
        free(then_line);
        free(else_line);
        return -1;
    }

    struct command *cmds = calloc(1, sizeof(*cmds));
    if (!cmds) {
        free(cond_line);
        free(then_line);
        free(else_line);
        return -1;
    }

    cmds[0].type = CMD_IF;
    cmds[0].cond_cmds = NULL;
    cmds[0].cond_ncmds = 0;
    cmds[0].then_cmds = NULL;
    cmds[0].then_ncmds = 0;
    cmds[0].else_cmds = NULL;
    cmds[0].else_ncmds = 0;

    if (parse_line_simple(cond_line, &cmds[0].cond_cmds, &cmds[0].cond_ncmds) != 0 || cmds[0].cond_ncmds == 0) {
        free(cond_line);
        free(then_line);
        free(else_line);
        free_commands(cmds, 1);
        return -1;
    }
    if (parse_line_simple(then_line, &cmds[0].then_cmds, &cmds[0].then_ncmds) != 0 || cmds[0].then_ncmds == 0) {
        free(cond_line);
        free(then_line);
        free(else_line);
        free_commands(cmds, 1);
        return -1;
    }

    if (branch_type == 1) {
        if (parse_line_simple(else_line, &cmds[0].else_cmds, &cmds[0].else_ncmds) != 0) {
            free(cond_line);
            free(then_line);
            free(else_line);
            free_commands(cmds, 1);
            return -1;
        }
    } else if (branch_type == 2) {
        struct command *nested_if = NULL;
        int nested_ncmds = 0;
        if (parse_if_tokens(tokens + branch_end, ntokens - branch_end, &nested_if, &nested_ncmds) != 0 || nested_ncmds == 0) {
            free(cond_line);
            free(then_line);
            free(else_line);
            free_commands(cmds, 1);
            return -1;
        }
        cmds[0].else_cmds = nested_if;
        cmds[0].else_ncmds = nested_ncmds;
    }

    free(cond_line);
    free(then_line);
    free(else_line);

    *cmds_out = cmds;
    *ncmds_out = 1;
    return 0;
}

static int parse_for_tokens(char **tokens, int ntokens, struct command **cmds_out, int *ncmds_out) {
    if (ntokens < 6) {
        fprintf(stderr, "syntax error: expected for loop\n");
        return -1;
    }
    const char *var = tokens[1];
    int in_pos = -1;
    int do_pos = -1;
    int done_pos = -1;

    for (int i = 2; i < ntokens; i++) {
        if (strcmp(tokens[i], "in") == 0) {
            in_pos = i;
            break;
        }
    }
    if (in_pos < 0) {
        fprintf(stderr, "syntax error: expected 'in' in for loop\n");
        return -1;
    }
    for (int i = in_pos + 1; i < ntokens; i++) {
        if (strcmp(tokens[i], "do") == 0) {
            do_pos = i;
            break;
        }
    }
    if (do_pos < 0) {
        fprintf(stderr, "syntax error: expected 'do' in for loop\n");
        return -1;
    }
    for (int i = do_pos + 1; i < ntokens; i++) {
        if (strcmp(tokens[i], "done") == 0) {
            done_pos = i;
            break;
        }
    }
    if (done_pos < 0) {
        fprintf(stderr, "syntax error: expected 'done' in for loop\n");
        return -1;
    }

    struct command *cmds = calloc(1, sizeof(*cmds));
    if (!cmds) return -1;

    cmds[0].type = CMD_FOR;
    cmds[0].for_var = strdup(var);
    if (!cmds[0].for_var) {
        free(cmds);
        return -1;
    }

    cmds[0].for_values = NULL;
    cmds[0].for_nvalues = 0;
    for (int i = in_pos + 1; i < do_pos; i++) {
        if (strcmp(tokens[i], ";") == 0) continue;
        char **tmp = realloc(cmds[0].for_values, (size_t)(cmds[0].for_nvalues + 1) * sizeof(*tmp));
        if (!tmp) {
            free_commands(cmds, 1);
            return -1;
        }
        cmds[0].for_values = tmp;
        cmds[0].for_values[cmds[0].for_nvalues++] = strdup(tokens[i]);
        if (!cmds[0].for_values[cmds[0].for_nvalues - 1]) {
            free_commands(cmds, 1);
            return -1;
        }
    }

    char *body_line = join_tokens(tokens, do_pos + 1, done_pos);
    if (!body_line) {
        free_commands(cmds, 1);
        return -1;
    }

    cmds[0].for_body = NULL;
    cmds[0].for_body_ncmds = 0;
    cmds[0].for_body_line = body_line;

    *cmds_out = cmds;
    *ncmds_out = 1;
    return 0;
}

static int parse_case_tokens(char **tokens, int ntokens, struct command **cmds_out, int *ncmds_out) {
    if (ntokens < 5) {
        fprintf(stderr, "syntax error: expected case statement\n");
        return -1;
    }

    int in_pos = -1;
    for (int i = 1; i < ntokens; i++) {
        if (strcmp(tokens[i], "in") == 0) {
            in_pos = i;
            break;
        }
    }
    if (in_pos < 0 || in_pos != 2) {
        fprintf(stderr, "syntax error: expected 'case WORD in'\n");
        return -1;
    }

    char *case_word = strdup(tokens[1]);
    if (!case_word) return -1;

    struct command *cmds = calloc(1, sizeof(*cmds));
    if (!cmds) {
        free(case_word);
        return -1;
    }

    cmds[0].type = CMD_CASE;
    cmds[0].case_word = case_word;
    cmds[0].case_items = NULL;
    cmds[0].case_item_count = 0;

    int pos = in_pos + 1;
    while (pos < ntokens) {
        if (strcmp(tokens[pos], "esac") == 0) {
            break;
        }
        if (strcmp(tokens[pos], ";") == 0) {
            pos++;
            continue;
        }

        char *pattern = NULL;
        if (tokens[pos][strlen(tokens[pos]) - 1] == ')') {
            size_t name_len = strlen(tokens[pos]) - 1;
            pattern = malloc(name_len + 1);
            if (!pattern) {
                free_commands(cmds, 1);
                return -1;
            }
            memcpy(pattern, tokens[pos], name_len);
            pattern[name_len] = '\0';
            pos++;
        } else {
            if (pos + 1 >= ntokens || strcmp(tokens[pos + 1], ")") != 0) {
                fprintf(stderr, "syntax error: expected ')' after pattern\n");
                free_commands(cmds, 1);
                return -1;
            }
            pattern = strdup(tokens[pos]);
            if (!pattern) {
                free_commands(cmds, 1);
                return -1;
            }
            pos += 2;
        }

        int body_start = pos;
        while (pos < ntokens && strcmp(tokens[pos], ";;") != 0 && strcmp(tokens[pos], "esac") != 0) {
            pos++;
        }

        if (body_start == pos) {
            fprintf(stderr, "syntax error: empty case body\n");
            free(pattern);
            free_commands(cmds, 1);
            return -1;
        }

        char *body_line = join_tokens(tokens, body_start, pos);
        if (!body_line) {
            free(pattern);
            free_commands(cmds, 1);
            return -1;
        }

        struct command *branch_cmds = NULL;
        int branch_ncmds = 0;
        if (parse_line_simple(body_line, &branch_cmds, &branch_ncmds) != 0 || branch_ncmds == 0) {
            free(body_line);
            free(pattern);
            free_commands(cmds, 1);
            return -1;
        }
        free(body_line);

        struct case_item *tmp_items = realloc(cmds[0].case_items, (size_t)(cmds[0].case_item_count + 1) * sizeof(*tmp_items));
        if (!tmp_items) {
            free(pattern);
            free_commands(cmds, 1);
            return -1;
        }
        cmds[0].case_items = tmp_items;
        cmds[0].case_items[cmds[0].case_item_count].pattern = pattern;
        cmds[0].case_items[cmds[0].case_item_count].cmds = branch_cmds;
        cmds[0].case_items[cmds[0].case_item_count].ncmds = branch_ncmds;
        cmds[0].case_item_count++;

        if (pos < ntokens && strcmp(tokens[pos], ";;") == 0) {
            pos++;
        }
    }

    if (pos >= ntokens || strcmp(tokens[pos], "esac") != 0) {
        fprintf(stderr, "syntax error: expected 'esac'\n");
        free_commands(cmds, 1);
        return -1;
    }

    *cmds_out = cmds;
    *ncmds_out = 1;
    return 0;
}

static int parse_function_tokens(char **tokens, int ntokens, struct command **cmds_out, int *ncmds_out) {
    if (ntokens < 5) {
        fprintf(stderr, "syntax error: expected function definition\n");
        return -1;
    }
    if (strcmp(tokens[1], "(") != 0 || strcmp(tokens[2], ")") != 0 || strcmp(tokens[3], "{") != 0) {
        return -1;
    }
    if (strcmp(tokens[ntokens - 1], "}") != 0) {
        fprintf(stderr, "syntax error: expected '}' at end of function definition\n");
        return -1;
    }

    char *name = strdup(tokens[0]);
    if (!name) return -1;

    char *body_line = join_tokens(tokens, 4, ntokens - 1);
    if (!body_line) {
        free(name);
        return -1;
    }

    struct command *body_cmds = NULL;
    int body_ncmds = 0;
    if (parse_line(body_line, &body_cmds, &body_ncmds) != 0) {
        free(name);
        free(body_line);
        return -1;
    }
    free(body_line);

    if (define_function(name, body_cmds, body_ncmds) != 0) {
        free(name);
        return -1;
    }
    free(name);

    *cmds_out = NULL;
    *ncmds_out = 0;
    return 0;
}

static int parse_compound_command(char **tokens, int ntokens, struct command **cmds_out, int *ncmds_out) {
    if (ntokens == 0) return -1;
    if (strcmp(tokens[0], "if") == 0) {
        return parse_if_tokens(tokens, ntokens, cmds_out, ncmds_out);
    }
    if (strcmp(tokens[0], "for") == 0) {
        return parse_for_tokens(tokens, ntokens, cmds_out, ncmds_out);
    }
    if (strcmp(tokens[0], "case") == 0) {
        return parse_case_tokens(tokens, ntokens, cmds_out, ncmds_out);
    }
    if (ntokens >= 5 && strcmp(tokens[1], "(") == 0 && strcmp(tokens[2], ")") == 0 && strcmp(tokens[3], "{") == 0) {
        return parse_function_tokens(tokens, ntokens, cmds_out, ncmds_out);
    }
    return -1;
}

int parse_line(char *line, struct command **cmds_out, int *ncmds_out) {
    char *line_copy = strdup(line);
    if (!line_copy) return -1;

    char **tokens = NULL;
    int ntokens = 0;
    int result = tokenize_line(line_copy, &tokens, &ntokens);
    if (result != 0) {
        free(line_copy);
        return -1;
    }

    int parsed = -1;
    if (ntokens == 0) {
        free_tokens(tokens, ntokens);
        free(line_copy);
        return 0;
    }
    if (strcmp(tokens[0], "if") == 0 || strcmp(tokens[0], "for") == 0 || strcmp(tokens[0], "case") == 0 ||
        (ntokens >= 5 && strcmp(tokens[1], "(") == 0 && strcmp(tokens[2], ")") == 0 && strcmp(tokens[3], "{") == 0)) {
        parsed = parse_compound_command(tokens, ntokens, cmds_out, ncmds_out);
    }

    free_tokens(tokens, ntokens);
    free(line_copy);

    if (parsed == 0) {
        return 0;
    }
    return parse_line_simple(line, cmds_out, ncmds_out);
}

static int parse_line_simple(char *line, struct command **cmds_out, int *ncmds_out) {
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
    cur->type = CMD_SIMPLE;
    cur->argv = NULL;
    cur->argc = 0;
    cur->in = NULL;
    cur->out = NULL;
    cur->append = 0;
    cur->op_after = 0;
    cur->cond_cmds = NULL;
    cur->cond_ncmds = 0;
    cur->then_cmds = NULL;
    cur->then_ncmds = 0;
    cur->else_cmds = NULL;
    cur->else_ncmds = 0;
    cur->for_var = NULL;
    cur->for_values = NULL;
    cur->for_nvalues = 0;
    cur->for_body = NULL;
    cur->for_body_ncmds = 0;
    cur->case_word = NULL;
    cur->case_items = NULL;
    cur->case_item_count = 0;
    cur->func_name = NULL;
    cur->func_body = NULL;
    cur->func_ncmds = 0;
    cur->for_body_line = NULL;

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
                fprintf(stderr, "syntax error near unexpected token `|'\n");
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
            cur->type = CMD_SIMPLE;
            cur->argv = NULL;
            cur->argc = 0;
            cur->in = NULL;
            cur->out = NULL;
            cur->append = 0;
            cur->op_after = 0;
            cur->cond_cmds = NULL;
            cur->cond_ncmds = 0;
            cur->then_cmds = NULL;
            cur->then_ncmds = 0;
            cur->else_cmds = NULL;
            cur->else_ncmds = 0;
            cur->for_var = NULL;
            cur->for_values = NULL;
            cur->for_nvalues = 0;
            cur->for_body = NULL;
            cur->for_body_ncmds = 0;
            cur->case_word = NULL;
            cur->case_items = NULL;
            cur->case_item_count = 0;
            cur->func_name = NULL;
            cur->func_body = NULL;
            cur->func_ncmds = 0;
            continue;
        }

        if (strcmp(tok, "&&") == 0 || strcmp(tok, "||") == 0) {
            int op = (tok[0] == '&') ? 1 : 2;  /* 1=&&, 2=|| */
            free(tok);
            if (cur->argc == 0) {
                fprintf(stderr, "syntax error: expected command before operator\n");
                free_commands(cmds, ncmds);
                return -1;
            }
            
            /* Set the operator for current command */
            cur->op_after = op;
            
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
            cur->type = CMD_SIMPLE;
            cur->argv = NULL;
            cur->argc = 0;
            cur->in = NULL;
            cur->out = NULL;
            cur->append = 0;
            cur->op_after = 0;
            cur->cond_cmds = NULL;
            cur->cond_ncmds = 0;
            cur->then_cmds = NULL;
            cur->then_ncmds = 0;
            cur->else_cmds = NULL;
            cur->else_ncmds = 0;
            cur->for_var = NULL;
            cur->for_values = NULL;
            cur->for_nvalues = 0;
            cur->for_body = NULL;
            cur->for_body_ncmds = 0;
            cur->case_word = NULL;
            cur->case_items = NULL;
            cur->case_item_count = 0;
            cur->func_name = NULL;
            cur->func_body = NULL;
            cur->func_ncmds = 0;
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

        int need = cur->argc + 1;
        char **tmp = realloc(cur->argv, (size_t)(need + 1) * sizeof(*tmp));
        if (!tmp) {
            free_commands(cmds, ncmds);
            return -1;
        }
        cur->argv = tmp;
        cur->argv[cur->argc++] = tok;
        cur->argv[cur->argc] = NULL;

        if (cur->argc == 1) {
            const char *alias_value = get_alias(tok);
            if (alias_value) {
                char *alias_copy = strdup(alias_value);
                if (!alias_copy) {
                    free_commands(cmds, ncmds);
                    return -1;
                }

                char *alias_sp = alias_copy;
                char *alias_tok;
                int alias_pos = 0;

                while ((alias_tok = next_token(&alias_sp)) && alias_pos < 100) {
                    if (alias_pos == 0) {
                        free(cur->argv[0]);
                        cur->argv[0] = alias_tok;
                    } else {
                        int new_need = cur->argc + 1;
                        char **new_tmp = realloc(cur->argv, (size_t)(new_need + 1) * sizeof(*new_tmp));
                        if (!new_tmp) {
                            free(alias_copy);
                            free_commands(cmds, ncmds);
                            return -1;
                        }
                        cur->argv = new_tmp;
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
        free_commands(cmds, ncmds);
        fprintf(stderr, "syntax error: unexpected end of line\n");
        return -1;
    }

    *cmds_out = cmds;
    *ncmds_out = ncmds;
    return 0;
}

static void free_command(struct command *cmd) {
    if (!cmd) return;

    if (cmd->argv) {
        for (int j = 0; cmd->argv[j]; j++) {
            free(cmd->argv[j]);
        }
        free(cmd->argv);
    }
    free(cmd->in);
    free(cmd->out);
    free(cmd->for_var);

    if (cmd->for_values) {
        for (int i = 0; i < cmd->for_nvalues; i++) {
            free(cmd->for_values[i]);
        }
        free(cmd->for_values);
    }
    free(cmd->for_body_line);

    if (cmd->cond_cmds) {
        free_commands(cmd->cond_cmds, cmd->cond_ncmds);
    }
    if (cmd->then_cmds) {
        free_commands(cmd->then_cmds, cmd->then_ncmds);
    }
    if (cmd->else_cmds) {
        free_commands(cmd->else_cmds, cmd->else_ncmds);
    }
    if (cmd->for_body) {
        free_commands(cmd->for_body, cmd->for_body_ncmds);
    }
    free(cmd->case_word);
    if (cmd->case_items) {
        for (int i = 0; i < cmd->case_item_count; i++) {
            free(cmd->case_items[i].pattern);
            free_commands(cmd->case_items[i].cmds, cmd->case_items[i].ncmds);
        }
        free(cmd->case_items);
    }
    free(cmd->func_name);
    if (cmd->func_body) {
        free_commands(cmd->func_body, cmd->func_ncmds);
    }
}

void free_commands(struct command *cmds, int ncmds) {
    if (!cmds) {
        return;
    }
    for (int i = 0; i < ncmds; i++) {
        free_command(&cmds[i]);
    }
    free(cmds);
}

static int run_command(struct command *cmd, int *exit_requested);

int execute_commands(struct command *cmds, int ncmds, int *exit_requested) {
    if (ncmds == 0) {
        return 0;
    }

    if (ncmds == 1 && cmds[0].type != CMD_SIMPLE) {
        int status = run_command(&cmds[0], exit_requested);
        last_exit_status = status;
        return status;
    }

    if (ncmds == 1 && cmds[0].argc > 0 && is_shell_function(cmds[0].argv[0])) {
        int status = run_shell_function(cmds[0].argv[0]);
        last_exit_status = status;
        return status;
    }

    if (ncmds == 1 && cmds[0].argc > 0 && is_builtin(cmds[0].argv[0])) {
        int status = run_builtin_with_redirection(&cmds[0], exit_requested);
        last_exit_status = status;
        return status;
    }

    /* If no logical operators are present, execute the full pipeline or command list directly */
    int has_logical = 0;
    for (int i = 0; i < ncmds; i++) {
        if (cmds[i].op_after != 0) {
            has_logical = 1;
            break;
        }
    }

    if (!has_logical) {
        int status = run_pipeline(cmds, ncmds);
        if (exit_requested) {
            *exit_requested = 0;
        }
        last_exit_status = status;
        return status;
    }

    int status = 0;
    int i = 0;
    while (i < ncmds) {
        int start = i;
        int end = i;
        while (end < ncmds - 1 && cmds[end].op_after == 0) {
            end++;
        }

        int segment_len = end - start + 1;
        status = run_pipeline(&cmds[start], segment_len);
        if (exit_requested) {
            *exit_requested = 0;
        }

        if (end < ncmds - 1) {
            if (cmds[end].op_after == 1 && status != 0) {
                break;
            }
            if (cmds[end].op_after == 2 && status == 0) {
                break;
            }
        }

        i = end + 1;
    }

    last_exit_status = status;
    return status;
}

static int run_command(struct command *cmd, int *exit_requested) {
    if (!cmd) {
        return 0;
    }

    switch (cmd->type) {
        case CMD_IF: {
            int status = execute_commands(cmd->cond_cmds, cmd->cond_ncmds, NULL);
            if (status == 0) {
                return execute_commands(cmd->then_cmds, cmd->then_ncmds, exit_requested);
            }
            if (cmd->else_cmds) {
                return execute_commands(cmd->else_cmds, cmd->else_ncmds, exit_requested);
            }
            return status;
        }
        case CMD_FOR: {
            int status = 0;
            char *saved = NULL;
            int had_saved = 0;
            char *old_val = getenv(cmd->for_var);
            if (old_val) {
                saved = strdup(old_val);
                had_saved = saved != NULL;
            }

            for (int i = 0; i < cmd->for_nvalues; i++) {
                setenv(cmd->for_var, cmd->for_values[i], 1);
                struct command *body_cmds = NULL;
                int body_ncmds = 0;
                if (cmd->for_body_line && parse_line(cmd->for_body_line, &body_cmds, &body_ncmds) == 0) {
                    status = execute_commands(body_cmds, body_ncmds, exit_requested);
                    free_commands(body_cmds, body_ncmds);
                    if (exit_requested && *exit_requested) {
                        break;
                    }
                }
            }

            if (had_saved) {
                setenv(cmd->for_var, saved, 1);
                free(saved);
            } else {
                unsetenv(cmd->for_var);
            }
            return status;
        }
        case CMD_CASE: {
            for (int i = 0; i < cmd->case_item_count; i++) {
                if (match_pattern(cmd->case_items[i].pattern, cmd->case_word)) {
                    return execute_commands(cmd->case_items[i].cmds, cmd->case_items[i].ncmds, exit_requested);
                }
            }
            return 0;
        }
        case CMD_FUNCTION_DEF:
            return 0;
        case CMD_SIMPLE:
            if (cmd->argc > 0 && is_shell_function(cmd->argv[0])) {
                return run_shell_function(cmd->argv[0]);
            }
            return run_pipeline(cmd, 1);
    }
    return 0;
}

int run_pipeline(struct command *cmds, int ncmds) {
    if (ncmds <= 0) { return 0; }

    int prev_fd = -1;
    pid_t *pids = calloc((size_t)ncmds, sizeof(*pids));
    if (!pids) {
        return -1;
    }

    for (int i = 0; i < ncmds; i++) {
        int pipe_fd[2] = {-1, -1};
        if (i < ncmds - 1) {
            if (pipe(pipe_fd) != 0) {
                perror("pipe");
                free(pids);
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
            free(pids);
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

            if (is_shell_function(cmds[i].argv[0])) {
                int status = run_shell_function(cmds[i].argv[0]);
                _exit(status);
            }

            if (is_builtin(cmds[i].argv[0])) {
                int builtin_status = run_builtin(&cmds[i], NULL);
                _exit(builtin_status);
            }

            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "%s: %s\n", cmds[i].argv[0],
                    strerror(errno));
            _exit(127);
        }

        pids[i] = pid;

        if (prev_fd != -1) {
            close(prev_fd);
        }
        if (pipe_fd[1] != -1) {
            close(pipe_fd[1]);
        }
        prev_fd = pipe_fd[0];
    }

    int status = 0;
    int exit_status = 0;
    for (int i = ncmds - 1; i >= 0; i--) {
        if (waitpid(pids[i], &status, 0) < 0) {
            continue;
        }
        if (i == ncmds - 1) {
            exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }

    free(pids);
    last_exit_status = exit_status;
    return exit_status;
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