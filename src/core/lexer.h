#ifndef LEXER_H
#define LEXER_H
#include <stdlib.h>
#include <string.h>
#include "parser.tab.h"

char *m_strdup(char *s)
{
    char *d = (char *)malloc(strlen(s) + 1);
    if (d)
    {
        strcpy(d, s);
    }
    return d;
}

#endif