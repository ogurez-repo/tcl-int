#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/validator/internal.h"

typedef struct ExprParser
{
    const char *text;
    size_t length;
    size_t index;
    int line;
    int column;
    ValidatorContext *context;
    TclError *error;
} ExprParser;

static int is_var_start_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_var_char(char c)
{
    return is_var_start_char(c) || (c >= '0' && c <= '9');
}

static void expr_skip_ws(ExprParser *parser)
{
    while (parser->index < parser->length && isspace((unsigned char)parser->text[parser->index]))
    {
        parser->index++;
    }
}

static int expr_error(ExprParser *parser, const char *message)
{
    tcl_error_set(parser->error, TCL_ERROR_SYNTAX, parser->line, parser->column, message);
    return 0;
}

static int expr_match_char(ExprParser *parser, char c)
{
    expr_skip_ws(parser);
    if (parser->index < parser->length && parser->text[parser->index] == c)
    {
        parser->index++;
        return 1;
    }

    return 0;
}

static int expr_match_text(ExprParser *parser, const char *text)
{
    size_t len = strlen(text);

    expr_skip_ws(parser);
    if (parser->index + len > parser->length)
    {
        return 0;
    }

    if (strncmp(parser->text + parser->index, text, len) != 0)
    {
        return 0;
    }

    parser->index += len;
    return 1;
}

static int expr_parse_expression(ExprParser *parser);

static int validate_var_name(ExprParser *parser, const char *name, size_t length)
{
    size_t index;

    if (length == 0 || !is_var_start_char(name[0]))
    {
        return expr_error(parser, "invalid variable reference");
    }

    for (index = 1; index < length; index++)
    {
        if (!is_var_char(name[index]))
        {
            return expr_error(parser, "array/namespace variables are not supported");
        }
    }

    return 1;
}

static int expr_parse_variable(ExprParser *parser)
{
    parser->index++;

    if (parser->index >= parser->length)
    {
        return expr_error(parser, "invalid variable reference");
    }

    if (parser->text[parser->index] == '{')
    {
        size_t start;

        parser->index++;
        start = parser->index;
        while (parser->index < parser->length && parser->text[parser->index] != '}')
        {
            parser->index++;
        }

        if (parser->index >= parser->length)
        {
            return expr_error(parser, "unterminated variable reference");
        }

        if (!validate_var_name(parser, parser->text + start, parser->index - start))
        {
            return 0;
        }

        parser->index++;
        return 1;
    }

    {
        size_t start = parser->index;

        if (!is_var_start_char(parser->text[parser->index]))
        {
            return expr_error(parser, "invalid variable reference");
        }

        parser->index++;
        while (parser->index < parser->length && is_var_char(parser->text[parser->index]))
        {
            parser->index++;
        }

        if (parser->index < parser->length && parser->text[parser->index] == '(')
        {
            return expr_error(parser, "array/namespace variables are not supported");
        }

        return validate_var_name(parser, parser->text + start, parser->index - start);
    }
}

static int expr_parse_number(ExprParser *parser)
{
    size_t start;

    expr_skip_ws(parser);
    start = parser->index;

    while (parser->index < parser->length && isdigit((unsigned char)parser->text[parser->index]))
    {
        parser->index++;
    }

    if (parser->index == start)
    {
        return 0;
    }

    return 1;
}

static int expr_parse_command_substitution(ExprParser *parser)
{
    size_t end;
    char *inner;
    int result;

    if (!validator_find_matching_bracket(parser->text, parser->index, &end))
    {
        return expr_error(parser, "unterminated command substitution");
    }

    inner = validator_copy_substring(parser->text + parser->index + 1, end - parser->index - 1);
    if (!inner)
    {
        return expr_error(parser, "out of memory");
    }

    result = validator_validate_script_text(parser->context, inner, parser->line, parser->column, parser->error);
    free(inner);
    if (!result)
    {
        return 0;
    }

    parser->index = end + 1;
    return 1;
}

static int expr_parse_primary(ExprParser *parser)
{
    expr_skip_ws(parser);

    if (parser->index >= parser->length)
    {
        return expr_error(parser, "invalid expression");
    }

    if (expr_parse_number(parser))
    {
        return 1;
    }

    if (parser->text[parser->index] == '$')
    {
        return expr_parse_variable(parser);
    }

    if (parser->text[parser->index] == '[')
    {
        return expr_parse_command_substitution(parser);
    }

    if (expr_match_char(parser, '('))
    {
        if (!expr_parse_expression(parser))
        {
            return 0;
        }

        if (!expr_match_char(parser, ')'))
        {
            return expr_error(parser, "missing ')' in expression");
        }

        return 1;
    }

    return expr_error(parser, "invalid expression");
}

static int expr_parse_unary(ExprParser *parser)
{
    expr_skip_ws(parser);

    if (expr_match_char(parser, '+') || expr_match_char(parser, '-') || expr_match_char(parser, '!'))
    {
        return expr_parse_unary(parser);
    }

    return expr_parse_primary(parser);
}

static int expr_parse_multiplicative(ExprParser *parser)
{
    if (!expr_parse_unary(parser))
    {
        return 0;
    }

    while (1)
    {
        if (expr_match_char(parser, '*') || expr_match_char(parser, '/') || expr_match_char(parser, '%'))
        {
            if (!expr_parse_unary(parser))
            {
                return expr_error(parser, "invalid expression");
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_additive(ExprParser *parser)
{
    if (!expr_parse_multiplicative(parser))
    {
        return 0;
    }

    while (1)
    {
        if (expr_match_char(parser, '+') || expr_match_char(parser, '-'))
        {
            if (!expr_parse_multiplicative(parser))
            {
                return expr_error(parser, "invalid expression");
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_relational(ExprParser *parser)
{
    if (!expr_parse_additive(parser))
    {
        return 0;
    }

    while (1)
    {
        if (expr_match_text(parser, "<=") || expr_match_text(parser, ">=") ||
            expr_match_char(parser, '<') || expr_match_char(parser, '>'))
        {
            if (!expr_parse_additive(parser))
            {
                return expr_error(parser, "invalid expression");
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_equality(ExprParser *parser)
{
    if (!expr_parse_relational(parser))
    {
        return 0;
    }

    while (1)
    {
        if (expr_match_text(parser, "==") || expr_match_text(parser, "!="))
        {
            if (!expr_parse_relational(parser))
            {
                return expr_error(parser, "invalid expression");
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_logical_and(ExprParser *parser)
{
    if (!expr_parse_equality(parser))
    {
        return 0;
    }

    while (1)
    {
        if (expr_match_text(parser, "&&"))
        {
            if (!expr_parse_equality(parser))
            {
                return expr_error(parser, "invalid expression");
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_expression(ExprParser *parser)
{
    if (!expr_parse_logical_and(parser))
    {
        return 0;
    }

    while (1)
    {
        if (expr_match_text(parser, "||"))
        {
            if (!expr_parse_logical_and(parser))
            {
                return expr_error(parser, "invalid expression");
            }
            continue;
        }

        return 1;
    }
}

int validator_validate_expression_text(
    ValidatorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error)
{
    ExprParser parser;

    parser.text = text;
    parser.length = strlen(text);
    parser.index = 0;
    parser.line = line;
    parser.column = column;
    parser.context = context;
    parser.error = error;

    if (!expr_parse_expression(&parser))
    {
        return 0;
    }

    expr_skip_ws(&parser);
    if (parser.index != parser.length)
    {
        return expr_error(&parser, "unexpected token in expression");
    }

    return 1;
}
