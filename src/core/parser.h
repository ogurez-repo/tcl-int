#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>

int yylex(void);
int yyparse(void);
void yyerror(const char *message);

#endif
