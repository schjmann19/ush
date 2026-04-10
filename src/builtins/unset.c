#include <stdlib.h>
#include <stdio.h>

#include "unset.h"

int builtin_unset(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (unsetenv(argv[i]) != 0) {
            perror("unset");
            return 1;
        }
    }
    return 0;
}