%option noyywrap

%{
#include "lexer.h"
%}

%%

[ \t]+              ;
"set"               { return SET; }
"put"               { return PUT; }

\$[^ \t\r\n]+        {
    yylval.str = m_strdup(yytext + 1);
    return VAR;
}

[^ \t\r\n]+         {
    yylval.str = m_strdup(yytext);
    return STRING;
}

[\r\n]+            ;


%%