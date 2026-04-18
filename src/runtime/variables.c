#include <stdlib.h>
#include <string.h>

#include "runtime/variables.h"

static Variable *create_variable(const char *name, const char *value)
{
    Variable *variable = (Variable *)malloc(sizeof(Variable));
    if (!variable)
    {
        return NULL;
    }

    variable->name = (char *)malloc(strlen(name) + 1);
    variable->value = (char *)malloc(strlen(value) + 1);
    if (!variable->name || !variable->value)
    {
        free(variable->name);
        free(variable->value);
        free(variable);
        return NULL;
    }

    strcpy(variable->name, name);
    strcpy(variable->value, value);

    return variable;
}

static void free_variable_data(void *data)
{
    Variable *variable = (Variable *)data;
    if (!variable)
    {
        return;
    }

    free(variable->name);
    free(variable->value);
    free(variable);
}

Variables *variables_create(void)
{
    Variables *variables = (Variables *)malloc(sizeof(Variables));
    if (!variables)
    {
        return NULL;
    }

    variables->head = NULL;
    variables->tail = NULL;
    return variables;
}

void variables_destroy(Variables *variables)
{
    if (!variables)
    {
        return;
    }

    free_list(variables->head, free_variable_data);
    free(variables);
}

int variables_set(Variables *variables, const char *name, const char *value)
{
    List *current_node;
    Variable *variable;

    if (!variables)
    {
        return 0;
    }

    current_node = variables->head;
    while (current_node)
    {
        variable = (Variable *)current_node->data;
        if (strcmp(variable->name, name) == 0)
        {
            char *new_value = (char *)malloc(strlen(value) + 1);
            if (!new_value)
            {
                return 0;
            }

            strcpy(new_value, value);
            free(variable->value);
            variable->value = new_value;
            return 1;
        }
        current_node = current_node->next;
    }

    variable = create_variable(name, value);
    if (!variable)
    {
        return 0;
    }

    if (!variables->head)
    {
        variables->head = new_list(variable);
        if (!variables->head)
        {
            free_variable_data(variable);
            return 0;
        }
        variables->tail = variables->head;
        return 1;
    }

    variables->tail = append_list(variables->tail, variable);
    if (!variables->tail)
    {
        free_variable_data(variable);
        return 0;
    }

    return 1;
}

const char *variables_get(const Variables *variables, const char *name)
{
    List *current_node;

    if (!variables)
    {
        return NULL;
    }

    current_node = variables->head;
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
