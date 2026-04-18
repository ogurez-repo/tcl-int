#include "lexer.h"

char *duplicate_substring(const char *source, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (!copy)
    {
        return NULL;
    }

    memcpy(copy, source, length);
    copy[length] = '\0';
    return copy;
}

char *duplicate_token_text(const char *source)
{
    return duplicate_substring(source, strlen(source));
}

char *duplicate_quoted_text(const char *source, size_t length)
{
    if (length < 2)
    {
        return NULL;
    }

    char *result = (char *)malloc(length - 1);
    size_t read_index;
    size_t write_index;

    if (!result)
    {
        return NULL;
    }

    read_index = 1;
    write_index = 0;

    while (read_index + 1 < length)
    {
        if (source[read_index] == '\\' && read_index + 2 < length)
        {
            read_index++;
        }

        result[write_index++] = source[read_index++];
    }

    result[write_index] = '\0';
    return result;
}

