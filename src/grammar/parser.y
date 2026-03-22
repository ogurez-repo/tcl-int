%{
#include "parser.h"
#include "runtime/variables.h"
%}

%union {
    char *str;
}

%token <str> STRING
%token <str> VAR
%token <str> SET
%token <str> PUT

%%

program:
           /* empty */
    | program command
    ;

command:
    SET STRING STRING {
        variables_set($2, $3);
    }
    | PUT STRING {
        printf("%s\n", $2);
    }
    | PUT VAR {
        char *variable_value = variables_get($2);
        if (variable_value)
        {
            printf("%s\n", variable_value);
        }
        else
        {
            fprintf(stderr, "variable '$%s' not found\n", $2);
            YYERROR;
        }
    }
    ;
%%

void yyerror(const char *message)
{
    fprintf(stderr, "parse error: %s\n", message);
}
