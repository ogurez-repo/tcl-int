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

static void print_usage(FILE *stream, const char *program)
{
    fprintf(stream, "usage: %s [--run|--check] [script-file]\n", program);
}

static int parse_arguments(int argc, char **argv, ExecutionMode *mode, const char **script_path)
{
    int i;

    *mode = EXEC_MODE_RUN;
    *script_path = NULL;

    for (i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];

        if (strcmp(arg, "--run") == 0)
        {
            *mode = EXEC_MODE_RUN;
            continue;
        }

        if (strcmp(arg, "--check") == 0)
        {
            *mode = EXEC_MODE_CHECK;
            continue;
        }

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
        {
            print_usage(stdout, argv[0]);
            return 0;
        }

        if (arg[0] == '-' && arg[1] != '\0')
        {
            fprintf(stderr, "unknown option: %s\n", arg);
            print_usage(stderr, argv[0]);
            return 0;
        }

        if (*script_path != NULL)
        {
            fprintf(stderr, "unexpected argument: %s\n", arg);
            print_usage(stderr, argv[0]);
            return 0;
        }

        *script_path = arg;
    }

    return 1;
}

static char *read_all_stream(FILE *stream)
{
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    int character;

    if (!buffer)
    {
        return NULL;
    }

    while ((character = fgetc(stream)) != EOF)
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

    if (ferror(stream))
    {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

static char *read_script_from_file(const char *path)
{
    FILE *stream;
    char *buffer;

    stream = fopen(path, "rb");
    if (!stream)
    {
        return NULL;
    }

    buffer = read_all_stream(stream);
    fclose(stream);
    return buffer;
}

int main(int argc, char **argv)
{
    char *script;
    TclError error;
    AstCommand *program;
    ExecutionMode mode;
    const char *script_path = NULL;
    ValidatorContext *validator = NULL;
    ExecutorContext *context = NULL;

    if (!parse_arguments(argc, argv, &mode, &script_path))
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

    if (script_path != NULL)
    {
        script = read_script_from_file(script_path);
        if (!script)
        {
            fprintf(stderr, "failed to read script: %s\n", script_path);
            executor_destroy(context);
            return EXIT_FAILURE;
        }
    }
    else
    {
        script = read_all_stream(stdin);
        if (!script)
        {
            fprintf(stderr, "failed to read input\n");
            executor_destroy(context);
            return EXIT_FAILURE;
        }
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
