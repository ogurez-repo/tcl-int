%option noyywrap
%x ARG

%{
#include "lexer.h"

static int command_token_from_text(const char *text)
{
    if (strcmp(text, "set") == 0)
    {
        return SET;
    }

    if (strcmp(text, "put") == 0)
    {
        return PUT;
    }

    return 0;
}
%}

%%

<INITIAL,ARG>[ \t]+      ;

<INITIAL>"set"          {
    BEGIN(ARG);
    return SET;
}

<INITIAL>"put"          {
    BEGIN(ARG);
    return PUT;
}

<INITIAL>\"[^ \t\r\n]*\" {
    int command_token;
    char *string = interpolate_token_text(yytext);

    if (string == NULL)
    {
        fprintf(stderr, "Error: Failed to interpolate string: %s\n", yytext);
        exit(1);
    }

    command_token = command_token_from_text(string);
    if (command_token != 0)
    {
        free(string);
        BEGIN(ARG);
        return command_token;
    }

    yylval.str = string;
    BEGIN(ARG);
    return STRING;
}

<ARG>\"[^ \t\r\n]*\" {
    char *string = interpolate_token_text(yytext);

    if (string == NULL)
    {
        fprintf(stderr, "Error: Failed to interpolate string: %s\n", yytext);
        exit(1);
    }

    yylval.str = string;
    return STRING;
}

<INITIAL>\$[^ \t\r\n]+ {
    yylval.str = duplicate_token_text(yytext + 1);
    BEGIN(ARG);
    return VAR;
}

<ARG>\$[^ \t\r\n]+ {
    yylval.str = duplicate_token_text(yytext + 1);
    return VAR;
}

<INITIAL>[^ \t\r\n]+    {
    yylval.str = duplicate_token_text(yytext);
    BEGIN(ARG);
    return STRING;
}

<ARG>[^ \t\r\n]+        {
    yylval.str = duplicate_token_text(yytext);
    return STRING;
}

<INITIAL,ARG>[\r\n]+     {
    BEGIN(INITIAL);
}

%%

void lexer_reset_state(void)
{
    BEGIN(INITIAL);
}