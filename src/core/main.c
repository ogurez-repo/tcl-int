#include <stdio.h>
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

typedef struct CliOptions
{
    ExecutionMode mode;
    const char *script_path;
} CliOptions;

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

static int parse_execution_mode(int argc, char **argv, CliOptions *options)
{
    int index;

    options->mode = EXEC_MODE_RUN;
    options->script_path = NULL;

    for (index = 1; index < argc; index++)
    {
        if (strcmp(argv[index], "--run") == 0)
        {
            options->mode = EXEC_MODE_RUN;
            continue;
        }

        if (strcmp(argv[index], "--check") == 0)
        {
            options->mode = EXEC_MODE_CHECK;
            continue;
        }

        if (argv[index][0] == '-' && argv[index][1] != '\0')
        {
            fprintf(stderr, "usage: %s [--run|--check] [script_file]\n", argv[0]);
            return 0;
        }

        if (options->script_path)
        {
            fprintf(stderr, "usage: %s [--run|--check] [script_file]\n", argv[0]);
            return 0;
        }

        options->script_path = argv[index];
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

static char *read_all_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    char *content;

    if (!file)
    {
        return NULL;
    }

    content = read_all_stream(file);
    fclose(file);
    return content;
}

int main(int argc, char **argv)
{
    TclError error;
    AstCommand *program;
    CliOptions options;
    ValidatorContext *validator = NULL;
    ExecutorContext *context = NULL;
    char *script = NULL;

    if (!parse_execution_mode(argc, argv, &options))
    {
        return EXIT_FAILURE;
    }

    if (options.script_path)
    {
        script = read_all_file(options.script_path);
        if (!script)
        {
            fprintf(stderr, "failed to read script file: %s\n", options.script_path);
            return EXIT_FAILURE;
        }
    }
    else
    {
        script = read_all_stream(stdin);
        if (!script)
        {
            fprintf(stderr, "failed to read input\n");
            return EXIT_FAILURE;
        }
    }

    if (options.mode == EXEC_MODE_RUN)
    {
        context = executor_create(stdin, stdout, stderr);
        if (!context)
        {
            fprintf(stderr, "failed to initialize executor\n");
            free(script);
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

    if (options.mode == EXEC_MODE_CHECK)
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
