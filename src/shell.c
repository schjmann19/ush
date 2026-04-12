#include "shell.h"
#include <stdio.h>
#include <unistd.h>

char *history[MAX_HISTORY];
int hist_count = 0;

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
        execute_commands(sub_cmds, sub_ncmds, &sub_exit);
        free_commands(sub_cmds, sub_ncmds);
        if (sub_exit) {
            break;
        }
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
    if (*p == '|' || *p == '<' || *p == '>' || *p == ';' || *p == '(' || *p == ')' || *p == '{' || *p == '}') {
        if (*p == '>' && *(p + 1) == '>') {
            *sp = p + 2;
            return dup_token(start, 2);
        }
        if (*p == ';' && *(p + 1) == ';') {
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
        if (!in_quote && (isspace((unsigned char)*p) || *p == '|' || *p == '<' || *p == '>' || *p == ';' || *p == '(' || *p == ')' || *p == '{' || *p == '}')) {
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
        return run_command(&cmds[0], exit_requested);
    }

    if (ncmds == 1 && cmds[0].argc > 0 && is_shell_function(cmds[0].argv[0])) {
        return run_shell_function(cmds[0].argv[0]);
    }

    if (ncmds == 1 && cmds[0].argc > 0 && is_builtin(cmds[0].argv[0])) {
        return run_builtin_with_redirection(&cmds[0], exit_requested);
    }

    return run_pipeline(cmds, ncmds);
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
                int dummy_exit = 0;
                run_builtin(&cmds[i], &dummy_exit);
                _exit(0);
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