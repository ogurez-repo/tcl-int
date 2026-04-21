#include <stdlib.h>

#include "core/ast.h"
#include "core/errors.h"
#include "core/script_parser.h"
#include "runtime/executor.h"

static char *read_all_stdin(void)
{
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    int character;

    if (!buffer)
    {
        return NULL;
    }

    while ((character = fgetc(stdin)) != EOF)
    {
        if (length + 1 >= capacity)
        {
            size_t new_capacity = capacity * 2;
            char *resized = (char *)realloc(buffer, new_capacity);
            if (!resized)
            {
                free(buffer);
                return NULL;
            }

            buffer = resized;
            capacity = new_capacity;
        }

        buffer[length++] = (char)character;
    }

    if (ferror(stdin))
    {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

int main(void)
{
    char *script;
    TclError error;
    AstCommand *program;
    ExecutorContext *context;

    context = executor_create(stdout, stderr);
    if (!context)
    {
        fprintf(stderr, "failed to initialize executor\n");
        return EXIT_FAILURE;
    }

    script = read_all_stdin();
    if (!script)
    {
        fprintf(stderr, "failed to read input\n");
        executor_destroy(context);
        return EXIT_FAILURE;
    }

    tcl_error_clear(&error);

    program = parse_script(script, 1, 1, &error);
    if (!program && error.type != TCL_ERROR_NONE)
    {
        free(script);

        fprintf(
            stderr,
            "%s error at %d:%d: %s\n",
            tcl_error_type_name(error.type),
            error.line,
            error.column,
            error.message);

        executor_destroy(context);
        return EXIT_FAILURE;
    }

    if (!executor_execute(context, program, &error))
    {
        ast_command_free(program);
        free(script);

        fprintf(
            stderr,
            "%s error at %d:%d: %s\n",
            tcl_error_type_name(error.type),
            error.line,
            error.column,
            error.message);
        executor_destroy(context);
        return EXIT_FAILURE;
    }

    ast_command_free(program);
    free(script);

    executor_destroy(context);
    return 0;
}
