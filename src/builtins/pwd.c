#include <unistd.h>
#include <limits.h>
#include <stdio.h>

#include "pwd.h"

int builtin_pwd(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) {
        perror("pwd");
        return 1;
    }
    puts(buf);
    return 0;
}