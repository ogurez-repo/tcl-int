#include "var.h"

List *g_var_head = NULL;
List *g_var_tail = NULL;

void new_var(char *name, char *value)
{
    Var *var = (Var *)malloc(sizeof(Var));
    if (!var)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    var->name = name;
    var->value = value;

    if (!g_var_head)
    {
        g_var_head = new_list(var);
        g_var_tail = g_var_head;
        return;
    }

    g_var_tail = append_list(g_var_tail, var);
}

char *get_var_value(char *name)
{
    List *current = g_var_head;
    while (current)
    {
        Var *var = (Var *)current->data;
        if (strcmp(var->name, name) == 0)
        {
            return var->value;
        }
        current = current->next;
    }
    return NULL;
}
