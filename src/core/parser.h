#ifndef PARSER_H
#define PARSER_H

#include "core/ast.h"
#include "core/errors.h"

int yylex(void);
int yyparse(void);
void yyerror(const char *message);

void parser_begin(TclError *error);
AstCommand *parser_take_program(void);
void parser_discard_program(void);

#endif
