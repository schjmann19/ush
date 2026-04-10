#include <stdlib.h>
#include <time.h>
#include "_test_builtin.h"

int _test_builtin_(int argc, char **argv) {
    (void)argc;
    (void)argv;

    static const char *texts[] = {
        #include "../splashtexts.txt"
    };

    size_t count = sizeof(texts) / sizeof(texts[0]);

    const char *text = texts[rand() % count];

    fprintf(stdout, "%s", text);
    return 0;
}