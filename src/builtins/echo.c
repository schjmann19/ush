#include <unistd.h>  // write() syscall
#include <string.h>  // strlen()

#include "echo.h"

#define final_newline 1 // i guess i'll add -n support later, but for now always print newline

int echo(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        // write the argument string
        write(1, argv[i], strlen(argv[i]));
        // if it's not the last argument, write a space
        if (i < argc - 1) {
            write(1, " ", 1);
        }
    }
    // write a final newline character, like 'echo' does by default
    if (final_newline) {
        write(1, "\n", 1);
    }
    return 0;
}
