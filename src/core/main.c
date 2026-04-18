#include <stdlib.h>

#include "core/ast.h"
#include "core/errors.h"
#include "runtime/executor.h"
#include "parser.h"
#include "lexer.h"

typedef void *YY_BUFFER_STATE;

YY_BUFFER_STATE yy_scan_string(const char *yy_str);
void yy_delete_buffer(YY_BUFFER_STATE buffer_state);

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
    YY_BUFFER_STATE buffer_state;
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
    lexer_reset_state();
    lexer_set_line(1);
    parser_begin(&error);

    buffer_state = yy_scan_string(script);
    if (!buffer_state)
    {
        fprintf(stderr, "failed to initialize lexer buffer\n");
        free(script);
        executor_destroy(context);
        return EXIT_FAILURE;
    }

    if (yyparse() != 0)
    {
        program = parser_take_program();
        ast_command_free(program);
        yy_delete_buffer(buffer_state);
        free(script);

        if (error.type != TCL_ERROR_NONE)
        {
            fprintf(
                stderr,
                "%s error at %d:%d: %s\n",
                tcl_error_type_name(error.type),
                error.line,
                error.column,
                error.message);
        }

        executor_destroy(context);
        return EXIT_FAILURE;
    }

    program = parser_take_program();
    if (!executor_execute(context, program, &error))
    {
        ast_command_free(program);
        yy_delete_buffer(buffer_state);
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
    yy_delete_buffer(buffer_state);
    free(script);

    executor_destroy(context);
    return 0;
}
