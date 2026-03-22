#ifndef VAR_H
#define VAR_H
#include <stdlib.h>
#include <string.h>
#include "list/list.h"
typedef struct Var
{
    char *name;
    char *value;
} Var;

extern List *g_var_head;
extern List *g_var_tail;

void new_var(char *name, char *value);
char *get_var_value(char *name);

#endif