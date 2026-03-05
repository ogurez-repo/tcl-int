%option noyywrap

%{
#include <stdlib.h>
#include "parser.tab.h"
%}

%%
[0-9]+      { yylval.ival = atoi(yytext); return NUMBER; }
[ \t\r]+    { /* ignore whitespace */ }
\n          { return '\n'; }
"+"         { return '+'; }
.           { return yytext[0]; }
%%
