#include <unistd.h>
#include <signal.h>

#include "yes.h"

int builtin_yes(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    // restore SIGINT
    signal(SIGINT, SIG_DFL);
    loop {
        write(1, "y\n", 2);
    }
    return 0; // never reached
}