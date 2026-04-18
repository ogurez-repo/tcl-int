#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdio.h>

#include "core/ast.h"
#include "core/errors.h"
#include "runtime/variables.h"

typedef struct ExecutorContext
{
    Variables *variables;
    FILE *stdout_stream;
    FILE *stderr_stream;
} ExecutorContext;

/* Returns a heap-allocated context that must be destroyed with executor_destroy. */
ExecutorContext *executor_create(FILE *stdout_stream, FILE *stderr_stream);
void executor_destroy(ExecutorContext *context);

/* Executes AST command list and fills error on failure. */
int executor_execute(ExecutorContext *context, const AstCommand *program, TclError *error);

#endif
