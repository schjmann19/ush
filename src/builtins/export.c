#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "export.h"

int builtin_export(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        char *eq = strchr(arg, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *name = arg;
        const char *value = eq + 1;
        if (setenv(name, value, 1) != 0) {
            perror("export");
            return 1;
        }
    }
    return 0;
}