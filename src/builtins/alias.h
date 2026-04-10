#ifndef ALIAS_H
#define ALIAS_H

int builtin_alias(int argc, char *argv[]);
const char *get_alias(const char *name);
void free_aliases(void);

#endif
