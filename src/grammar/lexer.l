%option noyywrap

%{
#include "lexer.h"
%}

%%

[ \t]+              ;
"set"               { return SET; }
"put"               { return PUT; }

\$[^ \t\r\n]+        {
    yylval.str = duplicate_token_text(yytext + 1);
    return VAR;
}

[^ \t\r\n]+         {
    yylval.str = duplicate_token_text(yytext);
    return STRING;
}

[\r\n]+            ;


%%
