#include <stdio.h>
#include <stdlib.h>

#include "runtime/variables.h"

static List *s_variable_list_head = NULL;
static List *s_variable_list_tail = NULL;

static Variable *create_variable(char *name, char *value)
{
    Variable *variable = (Variable *)malloc(sizeof(Variable));
    if (!variable)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    variable->name = name;
    variable->value = value;
    return variable;
}

void variables_set(char *name, char *value)
{
    Variable *variable = create_variable(name, value);

    if (!s_variable_list_head)
    {
        s_variable_list_head = new_list(variable);
        s_variable_list_tail = s_variable_list_head;
        return;
    }

    s_variable_list_tail = append_list(s_variable_list_tail, variable);
}

char *variables_get(char *name)
{
    List *current_node = s_variable_list_head;
    while (current_node)
    {
        Variable *variable = (Variable *)current_node->data;
        if (strcmp(variable->name, name) == 0)
        {
            return variable->value;
        }
        current_node = current_node->next;
    }

    return NULL;
}
