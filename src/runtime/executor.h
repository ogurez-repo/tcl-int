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
    size_t arg_count;
    struct Procedure *next;
} Procedure;

typedef enum ExecCode
{
    EXEC_CODE_OK = 0,
    EXEC_CODE_ERROR = 1,
    EXEC_CODE_RETURN = 2,
    EXEC_CODE_BREAK = 3,
    EXEC_CODE_CONTINUE = 4
} ExecCode;

typedef struct ExecutorContext
{
    Variables *variables;
    Procedure *procedures;
    FILE *stdin_stream;
    FILE *stdout_stream;
    FILE *stderr_stream;
    int proc_depth;
    int loop_depth;
    char *result;
} ExecutorContext;

/* Returns a heap-allocated context that must be destroyed with executor_destroy. */
ExecutorContext *executor_create(FILE *stdin_stream, FILE *stdout_stream, FILE *stderr_stream);
void executor_destroy(ExecutorContext *context);

/* Executes AST command list and fills error on failure. */
int executor_execute(ExecutorContext *context, const AstCommand *program, TclError *error);

#endif
