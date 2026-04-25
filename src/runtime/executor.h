#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdio.h>

#include "core/ast.h"
#include "core/errors.h"
#include "runtime/variables.h"

typedef struct Procedure
{
    char *name;
    char *args_text;
    char *body_text;
    size_t required_count;
    size_t max_count;
    int variadic;
    struct Procedure *next;
} Procedure;

typedef struct ExecutorContext
{
    Variables *variables;
    Procedure *procedures;
    FILE *stdout_stream;
    FILE *stderr_stream;
    char *result;
} ExecutorContext;

/* Returns a heap-allocated context that must be destroyed with executor_destroy. */
ExecutorContext *executor_create(FILE *stdout_stream, FILE *stderr_stream);
void executor_destroy(ExecutorContext *context);

/* Executes AST command list and fills error on failure. */
int executor_execute(ExecutorContext *context, const AstCommand *program, TclError *error);

#endif
