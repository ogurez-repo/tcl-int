%{
#include "parser.h"
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
        new_var($2, $3);
    }
    | PUT STRING {
        printf("%s\n", $2);
    }
    | PUT VAR {
        char *value = get_var_value($2);
        if (value) {
            printf("%s\n", value);
        } else {
            fprintf(stderr, "variable '$%s' not found\n", $2);
            YYERROR;
        }
}
    ;
%%

void yyerror(const char *s) {
    fprintf(stderr, "parse error: %s\n", s);
}
