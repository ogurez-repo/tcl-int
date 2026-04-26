#include <stdlib.h>
#include <string.h>

#include "runtime/validator/internal.h"

Procedure *validator_find_procedure(ValidatorContext *context, const char *name)
{
    Procedure *procedure = context->procedures;

    while (procedure)
    {
        if (strcmp(procedure->name, name) == 0)
        {
            return procedure;
        }
        procedure = procedure->next;
    }

    return NULL;
}

int validator_has_procedure(const ValidatorContext *context, const char *name)
{
    Procedure *procedure;

    if (!context)
    {
        return 0;
    }

    procedure = context->procedures;
    while (procedure)
    {
        if (strcmp(procedure->name, name) == 0)
        {
            return 1;
        }
        procedure = procedure->next;
    }

    return 0;
}

int validator_add_or_update_procedure(
    ValidatorContext *context,
    const char *name,
    size_t arg_count,
    TclError *error,
    const AstWord *source)
{
    return validator_declare_procedure(
        context,
        name,
        arg_count,
        error,
        source->span.line,
        source->span.column);
}

int validator_declare_procedure(
    ValidatorContext *context,
    const char *name,
    size_t arg_count,
    TclError *error,
    int line,
    int column)
{
    Procedure *procedure = validator_find_procedure(context, name);

    if (procedure)
    {
        procedure->arg_count = arg_count;
        return 1;
    }

    procedure = (Procedure *)malloc(sizeof(Procedure));
    if (!procedure)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
        return 0;
    }

    procedure->name = validator_copy_string(name);
    if (!procedure->name)
    {
        free(procedure);
        tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
        return 0;
    }

    procedure->arg_count = arg_count;
    procedure->next = context->procedures;
    context->procedures = procedure;
    return 1;
}

static void free_procedures(Procedure *procedure)
{
    while (procedure)
    {
        Procedure *next = procedure->next;
        free(procedure->name);
        free(procedure);
        procedure = next;
    }
}

ValidatorContext *validator_create(void)
{
    ValidatorContext *context = (ValidatorContext *)malloc(sizeof(ValidatorContext));
    if (!context)
    {
        return NULL;
    }

    context->procedures = NULL;
    return context;
}

void validator_destroy(ValidatorContext *context)
{
    if (!context)
    {
        return;
    }

    free_procedures(context->procedures);
    free(context);
}
