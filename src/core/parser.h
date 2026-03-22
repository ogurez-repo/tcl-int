#ifndef PARSER_H
#define PARSER_H
#include <stdio.h>
#include "var.h"

int yylex(void);
void yyerror(const char *s);

#endif