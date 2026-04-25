#include <stdlib.h>
#include <string.h>

#include "core/ast.h"
#include "core/errors.h"
#include "core/script_parser.h"
#include "runtime/executor.h"
#include "runtime/validator.h"

typedef enum ExecutionMode
{
    EXEC_MODE_RUN,
    EXEC_MODE_CHECK
} ExecutionMode;

static void print_error(FILE *stream, const TclError *error)
{
    fprintf(
        stream,
        "%s error at %d:%d: %s\n",
        tcl_error_type_name(error->type),
        error->line,
        error->column,
        error->message);
}

static int parse_execution_mode(int argc, char **argv, ExecutionMode *mode)
{
    if (argc <= 1)
    {
        *mode = EXEC_MODE_RUN;
        return 1;
    }

    if (argc == 2)
    {
        if (strcmp(argv[1], "--run") == 0)
        {
            *mode = EXEC_MODE_RUN;
            return 1;
        }

        if (strcmp(argv[1], "--check") == 0)
        {
            *mode = EXEC_MODE_CHECK;
            return 1;
        }
    }

    fprintf(stderr, "usage: %s [--run|--check]\n", argv[0]);
    return 0;
}

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

int main(int argc, char **argv)
{
    char *script;
    TclError error;
    AstCommand *program;
    ExecutionMode mode;
    ValidatorContext *validator = NULL;
    ExecutorContext *context = NULL;

    if (!parse_execution_mode(argc, argv, &mode))
    {
        return EXIT_FAILURE;
    }

    if (mode == EXEC_MODE_RUN)
    {
        context = executor_create(stdout, stderr);
        if (!context)
        {
            fprintf(stderr, "failed to initialize executor\n");
            return EXIT_FAILURE;
        }
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
        print_error(stderr, &error);
        executor_destroy(context);
        return EXIT_FAILURE;
    }

    if (mode == EXEC_MODE_CHECK)
    {
        validator = validator_create();
        if (!validator)
        {
            ast_command_free(program);
            free(script);
            tcl_error_set(&error, TCL_ERROR_SYSTEM, 1, 1, "failed to initialize validator");
            print_error(stderr, &error);
            return EXIT_FAILURE;
        }

        if (!validator_validate_program(validator, program, &error))
        {
            validator_destroy(validator);
            ast_command_free(program);
            free(script);
            print_error(stderr, &error);
            return EXIT_FAILURE;
        }
    }
    else
    {
        if (!executor_execute(context, program, &error))
        {
            ast_command_free(program);
            free(script);
            print_error(stderr, &error);
            executor_destroy(context);
            return EXIT_FAILURE;
        }
    }

    validator_destroy(validator);
    ast_command_free(program);
    free(script);
    executor_destroy(context);
    return EXIT_SUCCESS;
}
