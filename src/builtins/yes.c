#include <unistd.h>
#include <signal.h>

#include "yes.h"

int builtin_yes(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    // restore SIGINT
    signal(SIGINT, SIG_DFL);
    char* yes = argv[1] ? argv[1] : "y\n";
    loop {
        write(1, yes, 2);
    }
    return 0; // never reached
}