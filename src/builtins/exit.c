#include "exit.h"

int builtin_exit(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return 2; // special code for exit
}