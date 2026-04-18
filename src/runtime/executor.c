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
    if (word->type == AST_WORD_BRACED)
    {
        *result = (char *)malloc(strlen(word->text) + 1);
        if (!*result)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        strcpy(*result, word->text);
        return 1;
    }

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

static void free_values(char **values, size_t count)
{
    size_t index;

    if (!values)
    {
        return;
    }

    for (index = 0; index < count; index++)
    {
        free(values[index]);
    }

    free(values);
}

static int collect_words(const AstCommand *command, const AstWord ***words, size_t *count)
{
    const AstWord *cursor;
    const AstWord **collected;
    size_t index;

    *count = 0;
    cursor = command->words;
    while (cursor)
    {
        (*count)++;
        cursor = cursor->next;
    }

    collected = (const AstWord **)calloc(*count == 0 ? 1 : *count, sizeof(AstWord *));
    if (!collected)
    {
        return 0;
    }

    cursor = command->words;
    index = 0;
    while (cursor)
    {
        collected[index++] = cursor;
        cursor = cursor->next;
    }

    *words = collected;
    return 1;
}

static int evaluate_words(
    const ExecutorContext *context,
    const AstWord *head,
    size_t count,
    TclError *error,
    char ***values)
{
    const AstWord *word;
    char **evaluated;
    size_t index;

    evaluated = (char **)calloc(count == 0 ? 1 : count, sizeof(char *));
    if (!evaluated)
    {
        return 0;
    }

    word = head;
    for (index = 0; index < count; index++)
    {
        if (!evaluate_word(context, word, error, &evaluated[index]))
        {
            free_values(evaluated, count);
            return 0;
        }

        word = word->next;
    }

    *values = evaluated;
    return 1;
}

static int word_is_literal_keyword(const AstWord *word, const char *keyword)
{
    if (!word)
    {
        return 0;
    }

    if (word->type != AST_WORD_STRING && word->type != AST_WORD_BRACED)
    {
        return 0;
    }

    return strcmp(word->text, keyword) == 0;
}

static int syntax_error_at_word(TclError *error, const AstWord *word, const char *message)
{
    tcl_error_set(error, TCL_ERROR_SYNTAX, word->span.line, word->span.column, message);
    return 0;
}

static int validate_if_syntax(const AstWord **words, size_t count, TclError *error)
{
    size_t index = 1;

    if (count < 3)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, words[0]->span.line, words[0]->span.column, "if expects condition and body");
        return 0;
    }

    for (;;)
    {
        if (index >= count)
        {
            return syntax_error_at_word(error, words[count - 1], "if missing condition after elseif");
        }

        index++;

        if (index < count && word_is_literal_keyword(words[index], "then"))
        {
            index++;
        }

        if (index >= count)
        {
            return syntax_error_at_word(error, words[count - 1], "if missing body");
        }

        index++;

        if (index >= count)
        {
            return 1;
        }

        if (word_is_literal_keyword(words[index], "elseif"))
        {
            index++;
            continue;
        }

        if (word_is_literal_keyword(words[index], "else"))
        {
            index++;
            if (index >= count)
            {
                return syntax_error_at_word(error, words[count - 1], "if missing else body");
            }

            index++;
            if (index != count)
            {
                return syntax_error_at_word(error, words[index], "unexpected token after else body");
            }

            return 1;
        }

        return syntax_error_at_word(error, words[index], "unexpected token in if command");
    }
}

static int validate_while_syntax(const AstWord **words, size_t count, TclError *error)
{
    if (count != 3)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, words[0]->span.line, words[0]->span.column, "while expects exactly 2 arguments");
        return 0;
    }

    return 1;
}

static int validate_for_syntax(const AstWord **words, size_t count, TclError *error)
{
    if (count != 5)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, words[0]->span.line, words[0]->span.column, "for expects exactly 4 arguments");
        return 0;
    }

    return 1;
}

static int validate_proc_syntax(const AstWord **words, size_t count, TclError *error)
{
    if (count != 4)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, words[0]->span.line, words[0]->span.column, "proc expects exactly 3 arguments");
        return 0;
    }

    return 1;
}

static int execute_command(ExecutorContext *context, const AstCommand *command, TclError *error)
{
    const AstWord **words;
    char *command_name = NULL;
    char **values = NULL;
    size_t count;
    int success = 0;

    if (!collect_words(command, &words, &count))
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "out of memory");
        return 0;
    }

    if (count == 0)
    {
        free((void *)words);
        return 1;
    }

    if (!evaluate_word(context, words[0], error, &command_name))
    {
        free((void *)words);
        return 0;
    }

    if (strcmp(command_name, "set") == 0)
    {
        values = NULL;

        if (!evaluate_words(context, command->words, count, error, &values))
        {
            goto cleanup;
        }

        if (count != 3)
        {
            tcl_error_set(
                error,
                TCL_ERROR_SYNTAX,
                command->span.line,
                command->span.column,
                "set expects exactly 2 arguments");
            goto cleanup;
        }

        if (!variables_set(context->variables, values[1], values[2]))
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, command->span.line, command->span.column, "failed to set variable");
            goto cleanup;
        }

        success = 1;
        goto cleanup;
    }

    if (strcmp(command_name, "put") == 0)
    {
        values = NULL;

        if (!evaluate_words(context, command->words, count, error, &values))
        {
            goto cleanup;
        }

        if (count != 2)
        {
            tcl_error_set(
                error,
                TCL_ERROR_SYNTAX,
                command->span.line,
                command->span.column,
                "put expects exactly 1 argument");
            goto cleanup;
        }

        fprintf(context->stdout_stream, "%s\n", values[1]);
        success = 1;
        goto cleanup;
    }

    if (strcmp(command_name, "if") == 0)
    {
        success = validate_if_syntax(words, count, error);
        goto cleanup;
    }

    if (strcmp(command_name, "while") == 0)
    {
        success = validate_while_syntax(words, count, error);
        goto cleanup;
    }

    if (strcmp(command_name, "for") == 0)
    {
        success = validate_for_syntax(words, count, error);
        goto cleanup;
    }

    if (strcmp(command_name, "proc") == 0)
    {
        success = validate_proc_syntax(words, count, error);
        goto cleanup;
    }

    tcl_error_setf(
        error,
        TCL_ERROR_SEMANTIC,
        command->span.line,
        command->span.column,
        "unknown command '%s'",
        command_name);

cleanup:
    free(command_name);
    free_values(values, count);
    free((void *)words);
    return success;
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
