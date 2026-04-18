%option noyywrap nodefault noinput nounput
%x CMDSTART

%{
#include "lexer.h"

static TclError *s_error = NULL;
static int s_line = 1;
static int s_column = 1;

static void advance_line(const char *text, int length)
{
    int index = 0;
    int line_breaks = 0;

    while (index < length)
    {
        if (text[index] == '\r')
        {
            line_breaks++;
            if (index + 1 < length && text[index + 1] == '\n')
            {
                index++;
            }
        }
        else if (text[index] == '\n')
        {
            line_breaks++;
        }

        index++;
    }

    if (line_breaks == 0)
    {
        line_breaks = 1;
    }

    s_line += line_breaks;
    s_column = 1;
}

#define YY_USER_ACTION \
    yylloc.first_line = s_line; \
    yylloc.last_line = s_line; \
    yylloc.first_column = s_column; \
    yylloc.last_column = s_column + (int)yyleng - 1; \
    s_column += (int)yyleng;
%}

%%

<CMDSTART>[ \t]+      ;
<INITIAL>[ \t]+       ;

<CMDSTART>#[^\r\n]*  ;

<CMDSTART>;+ {
    return CMDSEP;
}

<INITIAL>;+ {
    BEGIN(CMDSTART);
    return CMDSEP;
}

<CMDSTART>[\r\n]+ {
    advance_line(yytext, (int)yyleng);
    return CMDSEP;
}

<INITIAL>[\r\n]+ {
    BEGIN(CMDSTART);
    advance_line(yytext, (int)yyleng);
    return CMDSEP;
}

<CMDSTART,INITIAL>\"([^\"\\\n]|\\.)*\" {
    yylval.str = duplicate_quoted_text(yytext, (size_t)yyleng);
    if (!yylval.str)
    {
        if (s_error)
        {
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, yylloc.first_line, yylloc.first_column, "out of memory");
        }
        return INVALID;
    }

    BEGIN(INITIAL);
    return STRING;
}

<CMDSTART,INITIAL>\"([^\"\\\n]|\\.)* {
    if (s_error)
    {
        tcl_error_set(
            s_error,
            TCL_ERROR_LEXICAL,
            yylloc.first_line,
            yylloc.first_column,
            "unterminated quoted string");
    }

    return INVALID;
}

<CMDSTART,INITIAL>\$\{[^}\r\n]+\} {
    yylval.str = duplicate_substring(yytext + 2, (size_t)yyleng - 3);
    if (!yylval.str)
    {
        if (s_error)
        {
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, yylloc.first_line, yylloc.first_column, "out of memory");
        }
        return INVALID;
    }

    BEGIN(INITIAL);
    return VAR;
}

<CMDSTART,INITIAL>\$[A-Za-z_][A-Za-z0-9_:]* {
    yylval.str = duplicate_token_text(yytext + 1);
    if (!yylval.str)
    {
        if (s_error)
        {
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, yylloc.first_line, yylloc.first_column, "out of memory");
        }
        return INVALID;
    }

    BEGIN(INITIAL);
    return VAR;
}

<CMDSTART,INITIAL>[^ \t\r\n\";]+ {
    yylval.str = duplicate_token_text(yytext);
    if (!yylval.str)
    {
        if (s_error)
        {
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, yylloc.first_line, yylloc.first_column, "out of memory");
        }
        return INVALID;
    }

    BEGIN(INITIAL);
    return STRING;
}

%%

void lexer_set_error(TclError *error)
{
    s_error = error;
}

void lexer_set_line(int line)
{
    if (line > 0)
    {
        s_line = line;
    }
}

void lexer_reset_state(void)
{
    BEGIN(CMDSTART);
    s_column = 1;
}
