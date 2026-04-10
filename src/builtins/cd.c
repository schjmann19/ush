#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "cd.h"

int builtin_cd(int argc, char *argv[]) {
    // `dir` is argc[1] : if its not there it's $HOME
    const char *dir = argc > 1 ? argv[1] : getenv("HOME");
    if (!dir) {
        fprintf(stderr, "cd: HOME not set\n");
        return 1;
    }
    if (chdir(dir) != 0) {
        perror("cd");
        return 1;
    }
    return 0;
}