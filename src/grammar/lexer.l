%option noyywrap nodefault
%x CMDSTART BRACED_STATE

%{
#include <ctype.h>
#include <stdio.h>

#include "lexer.h"

static int input(void);
static void yyunput(int character, char *buffer_pointer);
static int scanner_next_char_no_refill(void);

static TclError *s_error = NULL;
static int s_line = 1;
static int s_column = 1;
static char *s_braced_buffer = NULL;
static size_t s_braced_length = 0;
static size_t s_braced_capacity = 0;
static int s_braced_depth = 0;
static int s_braced_start_line = 1;
static int s_braced_start_column = 1;

typedef struct LexerBuffer
{
    char *data;
    size_t length;
    size_t capacity;
} LexerBuffer;

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

static int lexer_buffer_init(LexerBuffer *buffer, const char *text, size_t length)
{
    buffer->capacity = length + 64;
    buffer->length = length;
    buffer->data = (char *)malloc(buffer->capacity);
    if (!buffer->data)
    {
        return 0;
    }

    if (length > 0)
    {
        memcpy(buffer->data, text, length);
    }
    buffer->data[buffer->length] = '\0';
    return 1;
}

static void lexer_buffer_free(LexerBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static int lexer_buffer_append_char(LexerBuffer *buffer, char character)
{
    if (buffer->length + 2 > buffer->capacity)
    {
        size_t new_capacity = buffer->capacity * 2;
        char *resized = (char *)realloc(buffer->data, new_capacity);
        if (!resized)
        {
            return 0;
        }

        buffer->data = resized;
        buffer->capacity = new_capacity;
    }

    buffer->data[buffer->length++] = character;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static int is_word_delimiter(int character)
{
    return character == 0 ||
           character == ' ' ||
           character == '\t' ||
           character == '\r' ||
           character == '\n' ||
           character == ';';
}

static void advance_position_char(char character)
{
    if (character == '\n' || character == '\r')
    {
        s_line++;
        s_column = 1;
        return;
    }

    s_column++;
}

static int scan_escaped_state(const char *text, size_t length)
{
    int escaped = 0;
    size_t index;

    for (index = 0; index < length; index++)
    {
        if (escaped)
        {
            escaped = 0;
            continue;
        }

        if (text[index] == '\\')
        {
            escaped = 1;
        }
    }

    return escaped;
}

static void update_bracket_state(
    char character,
    int *bracket_depth,
    int *brace_depth,
    int *in_quote,
    int *escaped)
{
    if (*escaped)
    {
        *escaped = 0;
        return;
    }

    if (character == '\\')
    {
        *escaped = 1;
        return;
    }

    if (!*in_quote && *bracket_depth > 0 && character == '{')
    {
        (*brace_depth)++;
        return;
    }

    if (!*in_quote && *bracket_depth > 0 && character == '}' && *brace_depth > 0)
    {
        (*brace_depth)--;
        return;
    }

    if (*bracket_depth > 0 && *brace_depth == 0 && character == '"')
    {
        *in_quote = !*in_quote;
        return;
    }

    if (*brace_depth == 0 && !*in_quote && character == '[')
    {
        (*bracket_depth)++;
    }
    else if (*brace_depth == 0 && !*in_quote && character == ']' && *bracket_depth > 0)
    {
        (*bracket_depth)--;
    }
}

static char *scan_unquoted_word(const char *initial_text, size_t initial_length)
{
    LexerBuffer buffer;
    int bracket_depth = 0;
    int brace_depth = 0;
    int in_quote = 0;
    int escaped;
    size_t index;

    if (!lexer_buffer_init(&buffer, initial_text, initial_length))
    {
        return NULL;
    }

    escaped = 0;
    for (index = 0; index < buffer.length; index++)
    {
        update_bracket_state(buffer.data[index], &bracket_depth, &brace_depth, &in_quote, &escaped);
    }

    escaped = scan_escaped_state(buffer.data, buffer.length);

    while (1)
    {
        int character = scanner_next_char_no_refill();

        if (character == EOF || character == 0)
        {
            if (bracket_depth > 0 && s_error)
            {
                tcl_error_set(
                    s_error,
                    TCL_ERROR_SYNTAX,
                    yylloc.first_line,
                    yylloc.first_column,
                    "unterminated command substitution");

                /*
                 * Keep the partially scanned token and let the validator report
                 * a stable syntax error instead of forcing INVALID at EOF.
                 */
                yylloc.last_line = s_line;
                yylloc.last_column = s_column > 1 ? s_column - 1 : s_column;
                return buffer.data;
            }
            lexer_buffer_free(&buffer);
            return NULL;
        }

        if (is_word_delimiter(character) && bracket_depth == 0 && !escaped)
        {
            unput(character);
            break;
        }

        if (!lexer_buffer_append_char(&buffer, (char)character))
        {
            lexer_buffer_free(&buffer);
            return NULL;
        }

        advance_position_char((char)character);
        update_bracket_state((char)character, &bracket_depth, &brace_depth, &in_quote, &escaped);
    }

    yylloc.last_line = s_line;
    yylloc.last_column = s_column - 1;
    return buffer.data;
}

static int scanner_next_char_no_refill(void)
{
    *(yy_c_buf_p) = yy_hold_char;

    if (*(yy_c_buf_p) == YY_END_OF_BUFFER_CHAR)
    {
        if ((yy_c_buf_p) < &YY_CURRENT_BUFFER->yy_ch_buf[(yy_n_chars)])
        {
            *(yy_c_buf_p) = '\0';
        }
        else
        {
            return EOF;
        }
    }

    {
        int character = (unsigned char)*(yy_c_buf_p);
        yy_hold_char = *++(yy_c_buf_p);
        return character;
    }
}

static char *scan_quoted_word(void)
{
    LexerBuffer buffer;
    int escaped = 0;

    if (!lexer_buffer_init(&buffer, "", 0))
    {
        return NULL;
    }

    while (1)
    {
        int character = input();

        if (character == EOF)
        {
            if (s_error)
            {
                tcl_error_set(
                    s_error,
                    TCL_ERROR_LEXICAL,
                    yylloc.first_line,
                    yylloc.first_column,
                    "unterminated quoted string");
            }
            lexer_buffer_free(&buffer);
            return NULL;
        }

        if (character == '"' && !escaped)
        {
            advance_position_char((char)character);
            break;
        }

        if (!lexer_buffer_append_char(&buffer, (char)character))
        {
            lexer_buffer_free(&buffer);
            return NULL;
        }

        advance_position_char((char)character);

        if (escaped)
        {
            escaped = 0;
        }
        else if (character == '\\')
        {
            escaped = 1;
        }
    }

    yylloc.last_line = s_line;
    yylloc.last_column = s_column - 1;
    return buffer.data;
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
    return QUOTED;
}

<CMDSTART,INITIAL>\{\*\} {
    int next_character = scanner_next_char_no_refill();

    if (next_character != EOF)
    {
        unput(next_character);
    }

    BEGIN(INITIAL);
    if (next_character == EOF || is_word_delimiter(next_character))
    {
        return EXPAND_EMPTY;
    }

    return EXPAND_PREFIX;
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

<BRACED_STATE>\\ {
    if (!braced_append(yytext, (size_t)yyleng))
    {
        return braced_fail_oom();
    }
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

<CMDSTART,INITIAL>\$[A-Za-z_][A-Za-z0-9_:]*\([^ \t\r\n\";{}]*\) {
    yylval.str = duplicate_token_text(yytext + 1);
    if (!yylval.str)
    {
        if (s_error && s_error->type == TCL_ERROR_NONE)
        {
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, yylloc.first_line, yylloc.first_column, "out of memory");
        }
        return INVALID;
    }

    BEGIN(INITIAL);
    return VAR;
}

<CMDSTART,INITIAL>\$\{[^}\r\n]+\}[^ \t\r\n\";{}]+ {
    yylval.str = scan_unquoted_word(yytext, (size_t)yyleng);
    if (!yylval.str)
    {
        if (s_error && s_error->type == TCL_ERROR_NONE)
        {
            tcl_error_set(s_error, TCL_ERROR_SYSTEM, yylloc.first_line, yylloc.first_column, "out of memory");
        }
        return INVALID;
    }

    BEGIN(INITIAL);
    return STRING;
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
    return VAR_BRACED;
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
    yylval.str = scan_unquoted_word(yytext, (size_t)yyleng);
    if (!yylval.str)
    {
        if (s_error && s_error->type == TCL_ERROR_NONE)
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

void lexer_set_position(int line, int column)
{
    if (line > 0)
    {
        s_line = line;
    }

    if (column > 0)
    {
        s_column = column;
    }
}

void lexer_reset_state(void)
{
    braced_reset();
    BEGIN(CMDSTART);
    s_column = 1;
}
