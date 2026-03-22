#ifndef LEXER_H
#define LEXER_H

#include <stdlib.h>
#include <string.h>

#include "parser.tab.h"

static char *duplicate_token_text(const char *source)
{
    char *copy = (char *)malloc(strlen(source) + 1);
    if (copy)
    {
        strcpy(copy, source);
    }

    return copy;
}

#endif
