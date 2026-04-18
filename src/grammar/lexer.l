%option noyywrap nodefault noinput nounput
%x CMDSTART BRACED_STATE

%{
#include "lexer.h"

static TclError *s_error = NULL;
static int s_line = 1;
static int s_column = 1;
static char *s_braced_buffer = NULL;
static size_t s_braced_length = 0;
static size_t s_braced_capacity = 0;
static int s_braced_depth = 0;
static int s_braced_start_line = 1;
static int s_braced_start_column = 1;

static void braced_reset(void)
{
    free(s_braced_buffer);
    s_braced_buffer = NULL;
    s_braced_length = 0;
    s_braced_capacity = 0;
    s_braced_depth = 0;
}

static int braced_append(const char *text, size_t length)
{
    if (s_braced_length + length + 1 > s_braced_capacity)
    {
        size_t new_capacity = s_braced_capacity == 0 ? 64 : s_braced_capacity;

        while (s_braced_length + length + 1 > new_capacity)
        {
            new_capacity *= 2;
        }

        char *resized = (char *)realloc(s_braced_buffer, new_capacity);
        if (!resized)
        {
            return 0;
        }

        s_braced_buffer = resized;
        s_braced_capacity = new_capacity;
    }

    memcpy(s_braced_buffer + s_braced_length, text, length);
    s_braced_length += length;
    s_braced_buffer[s_braced_length] = '\0';
    return 1;
}

static int braced_fail_oom(void)
{
    if (s_error)
    {
        tcl_error_set(s_error, TCL_ERROR_SYSTEM, yylloc.first_line, yylloc.first_column, "out of memory");
    }

    braced_reset();
    return INVALID;
}

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

<CMDSTART,INITIAL>\{ {
    braced_reset();
    s_braced_depth = 1;
    s_braced_start_line = yylloc.first_line;
    s_braced_start_column = yylloc.first_column;
    BEGIN(BRACED_STATE);
}

<BRACED_STATE>\\\{ {
    if (!braced_append(yytext, (size_t)yyleng))
    {
        return braced_fail_oom();
    }
}

<BRACED_STATE>\\\} {
    if (!braced_append(yytext, (size_t)yyleng))
    {
        return braced_fail_oom();
    }
}

<BRACED_STATE>\\[^\r\n] {
    if (!braced_append(yytext, (size_t)yyleng))
    {
        return braced_fail_oom();
    }
}

<BRACED_STATE>\{ {
    s_braced_depth++;
    if (!braced_append(yytext, (size_t)yyleng))
    {
        return braced_fail_oom();
    }
}

<BRACED_STATE>\} {
    s_braced_depth--;
    if (s_braced_depth == 0)
    {
        yylval.str = duplicate_substring(s_braced_buffer ? s_braced_buffer : "", s_braced_length);
        if (!yylval.str)
        {
            return braced_fail_oom();
        }

        yylloc.first_line = s_braced_start_line;
        yylloc.first_column = s_braced_start_column;

        braced_reset();
        BEGIN(INITIAL);
        return BRACED;
    }

    if (!braced_append(yytext, (size_t)yyleng))
    {
        return braced_fail_oom();
    }
}

<BRACED_STATE>[^\\{}\r\n]+ {
    if (!braced_append(yytext, (size_t)yyleng))
    {
        return braced_fail_oom();
    }
}

<BRACED_STATE>[\r\n]+ {
    if (!braced_append(yytext, (size_t)yyleng))
    {
        return braced_fail_oom();
    }

    advance_line(yytext, (int)yyleng);
}

<BRACED_STATE><<EOF>> {
    if (s_error)
    {
        tcl_error_set(
            s_error,
            TCL_ERROR_LEXICAL,
            s_braced_start_line,
            s_braced_start_column,
            "unterminated braced string");
    }

    braced_reset();
    BEGIN(INITIAL);
    return INVALID;
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

<CMDSTART,INITIAL>[^ \t\r\n\";{}]+ {
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

<CMDSTART,INITIAL>. {
    if (s_error)
    {
        tcl_error_set(
            s_error,
            TCL_ERROR_LEXICAL,
            yylloc.first_line,
            yylloc.first_column,
            "invalid token");
    }

    return INVALID;
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
    braced_reset();
    BEGIN(CMDSTART);
    s_column = 1;
}
