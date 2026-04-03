#include "lexer.h"
#include "../runtime/variables.h"

static int is_variable_boundary(char character)
{
    return character == ' ' ||
           character == '\t' ||
           character == '\r' ||
           character == '\n' ||
           character == '"' ||
           character == '\0';
}

static int append_text(char **buffer, size_t *capacity, size_t *length, const char *text, size_t text_length)
{
    if (*length + text_length + 1 > *capacity)
    {
        size_t new_capacity = *capacity;
        while (*length + text_length + 1 > new_capacity)
        {
            new_capacity *= 2;
        }

        char *resized_buffer = (char *)realloc(*buffer, new_capacity);
        if (!resized_buffer)
        {
            return 0;
        }

        *buffer = resized_buffer;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    return 1;
}

static char *interpolate_text(char *source, int preserve_quotes)
{
    size_t source_length = strlen(source);
    size_t capacity = source_length + 1;
    char *result = (char *)malloc(capacity);
    size_t result_length = 0;
    size_t index = 0;

    if (!result)
    {
        return NULL;
    }

    while (index < source_length)
    {
        if (!preserve_quotes && source[index] == '"')
        {
            index++;
            continue;
        }

        if (source[index] != '$')
        {
            if (!append_text(&result, &capacity, &result_length, source + index, 1))
            {
                free(result);
                return NULL;
            }

            index++;
            continue;
        }

        index++;
        size_t variable_start = index;

        while (index < source_length && !is_variable_boundary(source[index]))
        {
            index++;
        }

        size_t variable_length = index - variable_start;
        char *variable_name;
        char *variable_value;

        if (variable_length == 0)
        {
            free(result);
            return NULL;
        }

        variable_name = (char *)malloc(variable_length + 1);
        if (!variable_name)
        {
            free(result);
            return NULL;
        }

        memcpy(variable_name, source + variable_start, variable_length);
        variable_name[variable_length] = '\0';

        variable_value = variables_get(variable_name);
        free(variable_name);

        if (!variable_value)
        {
            free(result);
            return NULL;
        }

        if (!append_text(&result, &capacity, &result_length, variable_value, strlen(variable_value)))
        {
            free(result);
            return NULL;
        }
    }

    result[result_length] = '\0';
    return result;
}

char *duplicate_token_text(char *source)
{
    char *copy = (char *)malloc(strlen(source) + 1);
    if (copy)
    {
        strcpy(copy, source);
    }

    return copy;
}

char *interpolate_token_text(char *source)
{
    return interpolate_text(source, 0);
}

char *interpolate_input_text(char *source)
{
    return interpolate_text(source, 1);
}
