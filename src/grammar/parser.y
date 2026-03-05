%{
#include "calc_runtime.h"

int yylex(void);
%}

%union {
    int ival;
}

%token <ival> NUMBER
%type <ival> expr

%%
input
    : /* empty */
    | input line
    ;

line
    : '\n'
    | expr '\n' { calc_print_result($1); }
    ;

expr
    : NUMBER                  { $$ = $1; }
    | expr '+' NUMBER         { $$ = calc_add($1, $3); }
    ;
%%
