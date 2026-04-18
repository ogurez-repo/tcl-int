#include <stdlib.h>
#include <string.h>

#include "runtime/executor.h"

static int append_text(char **buffer, size_t *capacity, size_t *length, const char *text, size_t text_length)
{
    if (*length + text_length + 1 > *capacity)
    {
        size_t new_capacity = *capacity;

        while (*length + text_length + 1 > new_capacity)
        {
            new_capacity *= 2;
        }

        char *resized = (char *)realloc(*buffer, new_capacity);
        if (!resized)
        {
            return 0;
        }

        *buffer = resized;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    (*buffer)[*length] = '\0';
    return 1;
}

static int is_variable_name_char(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') ||
           character == '_' ||
           character == ':';
}

static int resolve_string_word(
    const ExecutorContext *context,
    const AstWord *word,
    TclError *error,
    char **result)
{
    size_t source_index;
    size_t source_length;
    size_t capacity;
    size_t result_length;
    char *buffer;

    source_index = 0;
    source_length = strlen(word->text);
    capacity = source_length + 1;
    result_length = 0;

    buffer = (char *)malloc(capacity);
    if (!buffer)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
        return 0;
    }

    buffer[0] = '\0';

    while (source_index < source_length)
    {
        if (word->text[source_index] != '$')
        {
            if (!append_text(&buffer, &capacity, &result_length, word->text + source_index, 1))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
                return 0;
            }
            source_index++;
            continue;
        }

        source_index++;
        if (source_index >= source_length)
        {
            if (!append_text(&buffer, &capacity, &result_length, "$", 1))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
                return 0;
            }
            break;
        }

        size_t name_start;
        size_t name_length;

        if (word->text[source_index] == '{')
        {
            source_index++;
            name_start = source_index;

            while (source_index < source_length && word->text[source_index] != '}')
            {
                source_index++;
            }

            if (source_index >= source_length)
            {
                free(buffer);
                tcl_error_setf(
                    error,
                    TCL_ERROR_SYNTAX,
                    word->span.line,
                    word->span.column,
                    "unterminated braced variable reference");
                return 0;
            }

            name_length = source_index - name_start;
            source_index++;
        }
        else
        {
            name_start = source_index;
            while (source_index < source_length && is_variable_name_char(word->text[source_index]))
            {
                source_index++;
            }
            name_length = source_index - name_start;
        }

        if (name_length == 0)
        {
            if (!append_text(&buffer, &capacity, &result_length, "$", 1))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
                return 0;
            }
            continue;
        }

        char *name = (char *)malloc(name_length + 1);
        if (!name)
        {
            free(buffer);
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        memcpy(name, word->text + name_start, name_length);
        name[name_length] = '\0';

        const char *value = variables_get(context->variables, name);

        if (!value)
        {
            free(buffer);
            tcl_error_setf(
                error,
                TCL_ERROR_SEMANTIC,
                word->span.line,
                word->span.column,
                "variable '$%s' not found",
                name);
            free(name);
            return 0;
        }

        if (!append_text(&buffer, &capacity, &result_length, value, strlen(value)))
        {
            free(name);
            free(buffer);
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        free(name);
    }

    *result = buffer;
    return 1;
}

static int evaluate_word(
    const ExecutorContext *context,
    const AstWord *word,
    TclError *error,
    char **result)
{
    if (word->type == AST_WORD_VAR)
    {
        const char *value = variables_get(context->variables, word->text);
        if (!value)
        {
            tcl_error_setf(
                error,
                TCL_ERROR_SEMANTIC,
                word->span.line,
                word->span.column,
                "variable '$%s' not found",
                word->text);
            return 0;
        }

        *result = (char *)malloc(strlen(value) + 1);
        if (!*result)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        strcpy(*result, value);
        return 1;
    }

    return resolve_string_word(context, word, error, result);
}

ExecutorContext *executor_create(FILE *stdout_stream, FILE *stderr_stream)
{
    ExecutorContext *context = (ExecutorContext *)malloc(sizeof(ExecutorContext));
    if (!context)
    {
        return NULL;
    }

    context->variables = variables_create();
    if (!context->variables)
    {
        free(context);
        return NULL;
    }

    context->stdout_stream = stdout_stream;
    context->stderr_stream = stderr_stream;
    return context;
}

void executor_destroy(ExecutorContext *context)
{
    if (!context)
    {
        return;
    }

    variables_destroy(context->variables);
    free(context);
}

static int execute_command(ExecutorContext *context, const AstCommand *command, TclError *error)
{
    const AstWord *word;
    char **values;
    size_t count;
    size_t index;

    count = 0;
    word = command->words;
    while (word)
    {
        count++;
        word = word->next;
    }

    if (count == 0)
    {
        return 1;
    }

    values = (char **)calloc(count, sizeof(char *));
    if (!values)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
        return 0;
    }

    word = command->words;
    for (index = 0; index < count; index++)
    {
        if (!evaluate_word(context, word, error, &values[index]))
        {
            goto fail;
        }

        word = word->next;
    }

    if (strcmp(values[0], "set") == 0)
    {
        if (count != 3)
        {
            tcl_error_set(
                error,
                TCL_ERROR_SYNTAX,
                command->span.line,
                command->span.column,
                "set expects exactly 2 arguments");
            goto fail;
        }

        if (!variables_set(context->variables, values[1], values[2]))
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "failed to set variable");
            goto fail;
        }
    }
    else if (strcmp(values[0], "put") == 0)
    {
        if (count != 2)
        {
            tcl_error_set(
                error,
                TCL_ERROR_SYNTAX,
                command->span.line,
                command->span.column,
                "put expects exactly 1 argument");
            goto fail;
        }

        fprintf(context->stdout_stream, "%s\n", values[1]);
    }
    else
    {
        tcl_error_setf(
            error,
            TCL_ERROR_SEMANTIC,
            command->span.line,
            command->span.column,
            "unknown command '%s'",
            values[0]);
        goto fail;
    }

    for (index = 0; index < count; index++)
    {
        free(values[index]);
    }
    free(values);
    return 1;

fail:
    for (index = 0; index < count; index++)
    {
        free(values[index]);
    }
    free(values);
    return 0;
}

int executor_execute(ExecutorContext *context, const AstCommand *program, TclError *error)
{
    const AstCommand *command = program;

    while (command)
    {
        if (!execute_command(context, command, error))
        {
            return 0;
        }

        command = command->next;
    }

    return 1;
}
