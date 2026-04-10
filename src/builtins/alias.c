#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "alias.h"

#define MAX_ALIASES 999

struct alias_entry {
    char *name;
    char *value;
};

static struct alias_entry aliases[MAX_ALIASES];
static int alias_count = 0;

static int find_alias(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int builtin_alias(int argc, char *argv[]) {
    // alias name=value
    if (argc == 1) {
        // list all aliases
        for (int i = 0; i < alias_count; i++) {
            printf("alias %s='%s'\n", aliases[i].name, aliases[i].value);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *arg = strdup(argv[i]);
        if (!arg) {
            perror("alias");
            return 1;
        }

        char *eq = strchr(arg, '=');
        if (!eq) {
            // just list one alias
            int idx = find_alias(arg);
            if (idx >= 0) {
                printf("alias %s='%s'\n", aliases[idx].name, aliases[idx].value);
            } else {
                fprintf(stderr, "alias: %s: not found\n", arg);
            }
            free(arg);
            continue;
        }

        *eq = '\0';
        const char *name = arg;
        const char *value = eq + 1;

        if (!name[0] || !value[0]) {
            fprintf(stderr, "alias: invalid syntax\n");
            free(arg);
            return 1;
        }

        /* strip surrounding quotes from value if present */
        char *value_to_store = strdup(value);
        if (!value_to_store) {
            perror("alias");
            free(arg);
            return 1;
        }
        size_t vlen = strlen(value_to_store);
        if (vlen >= 2 && ((value_to_store[0] == '\'' && value_to_store[vlen-1] == '\'') ||
                          (value_to_store[0] == '"' && value_to_store[vlen-1] == '"'))) {
            memmove(value_to_store, value_to_store + 1, vlen - 2);
            value_to_store[vlen - 2] = '\0';
        }

        int idx = find_alias(name);
        if (idx >= 0) {
            // update existing alias
            free(aliases[idx].value);
            aliases[idx].value = value_to_store;
        } else {
            // add new alias
            if (alias_count >= MAX_ALIASES) {
                fprintf(stderr, "alias: too many aliases\n");
                free(arg);
                free(value_to_store);
                return 1;
            }
            aliases[alias_count].name = strdup(name);
            aliases[alias_count].value = value_to_store;
            if (!aliases[alias_count].name || !aliases[alias_count].value) {
                perror("alias");
                free(arg);
                free(value_to_store);
                return 1;
            }
            alias_count++;
        }

        free(arg);
    }

    return 0;
}

const char *get_alias(const char *name) {
    int idx = find_alias(name);
    if (idx >= 0) {
        return aliases[idx].value;
    }
    return NULL;
}

void free_aliases(void) {
    for (int i = 0; i < alias_count; i++) {
        free(aliases[i].name);
        free(aliases[i].value);
    }
    alias_count = 0;
}
