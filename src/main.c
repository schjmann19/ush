#include "shell.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    srand((unsigned int)time(NULL));

    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL);

    const char *hist_file = getenv("HIST_FILE");
    if (!hist_file) {
        hist_file = "./.ush_hist";
    }
    load_history(hist_file);
    
    /* source ushrc on startup */
    source_file(USHRC);

    loop {
        char *line = read_line();
        if (!line) {
            break;
        }

        char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            free(line);
            continue;
        }

        struct command *cmds = NULL;
        int ncmds = 0;
        if (parse_line(p, &cmds, &ncmds) != 0) {
            free_commands(cmds, ncmds);
            free(line);
            continue;
        }

        if (ncmds == 0) {
            free_commands(cmds, ncmds);
            free(line);
            continue;
        }

        int exit_requested = 0;
        if (ncmds == 1 && is_builtin(cmds[0].argv[0])) {
            run_builtin_with_redirection(&cmds[0], &exit_requested);
        } else {
            run_pipeline(cmds, ncmds);
        }

        free_commands(cmds, ncmds);
        free(line);

        if (exit_requested) {
            break;
        }
    }

    save_history(hist_file);
    return 0;
}
