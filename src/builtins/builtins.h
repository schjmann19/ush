#ifndef BUILTINS_H
#define BUILTINS_H

#include "cd.h"
#include "exit.h"
#include "pwd.h"
#include "export.h"
#include "unset.h"
#include "echo.h"
#include "command.h"
#include "type.h"
#include "dot.h"
#include "true.h"
#include "false.h"
#include "yes.h"
#include "_test_builtin.h"
#include "alias.h"

struct builtin {
    const char *name;
    int (*func)(int argc, char **argv);
};

extern const struct builtin builtins[];

const char *get_alias(const char *name);
void free_aliases(void);

#endif