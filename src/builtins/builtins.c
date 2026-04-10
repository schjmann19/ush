#include <stddef.h>

#include "builtins.h"

const struct builtin builtins[] = {
    {"cd", builtin_cd},
    {"exit", builtin_exit},
    {"pwd", builtin_pwd},
    {"export", builtin_export},
    {"unset", builtin_unset},
    {"echo", echo},
    {"command", builtin_command},
    {"type", builtin_type},
    {".", builtin_dot},
    {"true", builtin_true},
    {"false", builtin_false},
    {"yes", builtin_yes},
    {"_test_builtin_", _test_builtin_},
    {"alias", builtin_alias},
    {NULL, NULL}
};