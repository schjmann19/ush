#include "command.h"

// returns `3` :)
int builtin_command(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    // return 3 to indicate run external
    return 3;
}