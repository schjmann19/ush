#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "type.h"
#include "builtins.h"

static int is_builtin_type(const char *cmd) {
    if (!cmd) return 0;
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            return 1;
        }
    }
    return 0;
}

int builtin_type(int argc, char *argv[]) {
    int start_i = 1;
    int all = 0;
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        all = 1;
        start_i = 2;
    }
    for (int i = start_i; i < argc; i++) {
        const char *cmd = argv[i];
        int found = 0;
        const char *alias_value = get_alias(cmd);
        if (alias_value) {
            printf("%s is aliased to '%s'\n", cmd, alias_value);
            found = 1;
        }
        if (is_builtin_type(cmd)) {
            printf("%s is a shell builtin\n", cmd);
            found = 1;
        }
        if (!found || all) {
            // Search PATH
            const char *env_path = getenv("PATH");
            if (env_path) {
                char *path_copy = strdup(env_path);
                if (path_copy) {
                    char *dir = strtok(path_copy, ":");
                    while (dir) {
                        char fullpath[PATH_MAX];
                        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, cmd);
                        if (access(fullpath, X_OK) == 0) {
                            printf("%s is %s\n", cmd, fullpath);
                            found = 1;
                            if (!all) break;
                        }
                        dir = strtok(NULL, ":");
                    }
                    free(path_copy);
                }
            }
        }
        if (!found) {
            fprintf(stderr, "type: %s: not found\n", cmd);
            return 1;
        }
    }
    return 0;
}
