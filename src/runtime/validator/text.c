#include <stdlib.h>
#include <string.h>

#include "core/script_parser.h"
#include "runtime/validator/internal.h"

typedef struct TextBuffer
{
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

static int text_buffer_init(TextBuffer *buffer)
{
    buffer->capacity = 64;
    buffer->length = 0;
    buffer->data = (char *)malloc(buffer->capacity);
    if (!buffer->data)
    {
        return 0;
    }

    buffer->data[0] = '\0';
    return 1;
}

static void text_buffer_free(TextBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static int text_buffer_append(TextBuffer *buffer, const char *text, size_t length)
{
    if (buffer->length + length + 1 > buffer->capacity)
    {
        size_t new_capacity = buffer->capacity;
        char *resized;

        while (buffer->length + length + 1 > new_capacity)
        {
            new_capacity *= 2;
        }

        resized = (char *)realloc(buffer->data, new_capacity);
        if (!resized)
        {
            return 0;
        }

        buffer->data = resized;
        buffer->capacity = new_capacity;
    }

    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

int validator_find_matching_bracket(const char *text, size_t start, size_t *end)
{
    size_t index = start + 1;
    size_t length = strlen(text);
    int bracket_depth = 1;
    int brace_depth = 0;
    int in_quote = 0;
    int escaped = 0;

    while (index < length)
    {
        char character = text[index];

        if (escaped)
        {
            escaped = 0;
            index++;
            continue;
        }

        if (character == '\\')
        {
            escaped = 1;
            index++;
            continue;
        }

        if (!in_quote && character == '{')
        {
            brace_depth++;
            index++;
            continue;
        }

        if (!in_quote && character == '}' && brace_depth > 0)
        {
            brace_depth--;
            index++;
            continue;
        }

        if (brace_depth == 0 && character == '"')
        {
            in_quote = !in_quote;
            index++;
            continue;
        }

        if (brace_depth == 0 && character == '[')
        {
            bracket_depth++;
        }
        else if (brace_depth == 0 && character == ']')
        {
            bracket_depth--;
            if (bracket_depth == 0)
            {
                *end = index;
                return 1;
            }
        }

        index++;
    }

    return 0;
}

int validator_validate_command_substitutions_in_text(
    ValidatorContext *context,
    const char *text,
    int start_line,
    int start_column,
    TclError *error)
{
    size_t index = 0;
    size_t length = strlen(text);
    int line = start_line;
    int column = start_column;

    while (index < length)
    {
        if (text[index] == '[')
        {
            size_t end;
            char *inner;
            int inner_line = line;
            int inner_column = column + 1;
            int result;

            if (!validator_find_matching_bracket(text, index, &end))
            {
                tcl_error_set(error, TCL_ERROR_SYNTAX, line, column, "unterminated command substitution");
                return 0;
            }

            inner = validator_copy_substring(text + index + 1, end - index - 1);
            if (!inner)
            {
                tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
                return 0;
            }

            result = validator_validate_script_text(context, inner, inner_line, inner_column, error);
            free(inner);
            if (!result)
            {
                return 0;
            }

            while (index <= end)
            {
                validator_advance_position(text[index], &line, &column);
                index++;
            }
            continue;
        }

        validator_advance_position(text[index], &line, &column);
        index++;
    }

    return 1;
}

int validator_sanitize_script_text(
    ValidatorContext *context,
    const char *script,
    int start_line,
    int start_column,
    TclError *error,
    char **sanitized)
{
    TextBuffer buffer;
    size_t index = 0;
    size_t length = strlen(script);
    int line = start_line;
    int column = start_column;
    int brace_depth = 0;
    int in_quote = 0;
    int escaped = 0;

    if (!text_buffer_init(&buffer))
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, start_line, start_column, "out of memory");
        return 0;
    }

    while (index < length)
    {
        char character = script[index];

        if (!escaped && character == '[' && brace_depth == 0)
        {
            size_t end;
            char *inner;
            int result;

            if (!validator_find_matching_bracket(script, index, &end))
            {
                text_buffer_free(&buffer);
                tcl_error_set(error, TCL_ERROR_SYNTAX, line, column, "unterminated command substitution");
                return 0;
            }

            inner = validator_copy_substring(script + index + 1, end - index - 1);
            if (!inner)
            {
                text_buffer_free(&buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
                return 0;
            }

            result = validator_validate_script_text(context, inner, line, column + 1, error);
            free(inner);
            if (!result)
            {
                text_buffer_free(&buffer);
                return 0;
            }

            if (!text_buffer_append(&buffer, "cmdsubst", 8))
            {
                text_buffer_free(&buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
                return 0;
            }

            while (index <= end)
            {
                validator_advance_position(script[index], &line, &column);
                index++;
            }
            escaped = 0;
            continue;
        }

        if (!text_buffer_append(&buffer, script + index, 1))
        {
            text_buffer_free(&buffer);
            tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
            return 0;
        }

        if (escaped)
        {
            escaped = 0;
        }
        else if (character == '\\')
        {
            escaped = 1;
        }
        else if (!in_quote && character == '{')
        {
            brace_depth++;
        }
        else if (!in_quote && character == '}' && brace_depth > 0)
        {
            brace_depth--;
        }
        else if (brace_depth == 0 && character == '"')
        {
            in_quote = !in_quote;
        }

        validator_advance_position(character, &line, &column);
        index++;
    }

    *sanitized = buffer.data;
    return 1;
}

int validator_validate_script_text(
    ValidatorContext *context,
    const char *script,
    int start_line,
    int start_column,
    TclError *error)
{
    char *sanitized = NULL;
    AstCommand *program;
    int result;

    if (!validator_sanitize_script_text(context, script, start_line, start_column, error, &sanitized))
    {
        return 0;
    }

    program = parse_script(sanitized, start_line, start_column, error);
    free(sanitized);
    if (!program && error->type != TCL_ERROR_NONE)
    {
        return 0;
    }

    result = validator_validate_program(context, program, error);
    ast_command_free(program);
    return result;
}
