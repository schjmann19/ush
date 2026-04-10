#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "type.h"
#include "builtins.h"

static int commandv(const char *cmd, char *path, size_t size) {
    const char *env_path = getenv("PATH");
    if (!env_path) return 0;
    char *path_copy = strdup(env_path);
    if (!path_copy) return 0;
    char *dir = strtok(path_copy, ":");
    while (dir) {
        snprintf(path, size, "%s/%s", dir, cmd);
        if (access(path, X_OK) == 0) {
            free(path_copy);
            return 1;
        }
        dir = strtok(NULL, ":");
    }
    free(path_copy);
    return 0;
}

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
    for (int i = 1; i < argc; i++) {
        const char *cmd = argv[i];
        const char *alias_value = get_alias(cmd);
        if (alias_value) {
            printf("%s is aliased to '%s'\n", cmd, alias_value);
        } else if (is_builtin_type(cmd)) {
            printf("%s is a shell builtin\n", cmd);
        } else {
            char path[PATH_MAX];
            if (commandv(cmd, path, sizeof(path))) {
                printf("%s is %s\n", cmd, path);
            } else {
                fprintf(stderr, "type: %s: not found\n", cmd);
                return 1;
            }
        }
    }
    return 0;
}
