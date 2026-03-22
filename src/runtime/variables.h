#ifndef VARIABLES_H
#define VARIABLES_H

#include <string.h>
#include "core/list/list.h"

typedef struct Variable
{
    char *name;
    char *value;
} Variable;

void variables_set(char *name, char *value);
char *variables_get(const char *name);

#endif
