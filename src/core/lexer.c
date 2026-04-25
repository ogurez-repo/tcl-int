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

    if (!result)
    {
        return NULL;
    }

    memcpy(result, source + 1, length - 2);
    result[length - 2] = '\0';
    return result;
}
