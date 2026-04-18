#ifndef VARIABLES_H
#define VARIABLES_H

#include "core/list/list.h"

typedef struct Variable
{
    char *name;
    char *value;
} Variable;

typedef struct Variables
{
    List *head;
    List *tail;
} Variables;

/* Returns a heap-allocated store that must be destroyed with variables_destroy. */
Variables *variables_create(void);
void variables_destroy(Variables *variables);

/* Returns 1 on success, 0 on allocation/system failure. */
int variables_set(Variables *variables, const char *name, const char *value);
/* Returns internal pointer owned by store, valid until next mutation/destroy. */
const char *variables_get(const Variables *variables, const char *name);

#endif
