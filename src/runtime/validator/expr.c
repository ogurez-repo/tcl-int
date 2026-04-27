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

static int expr_match_char(ExprParser *parser, char character)
{
    expr_skip_ws(parser);
    if (parser->index < parser->length && parser->text[parser->index] == character)
    {
        parser->index++;
        return 1;
    }
    return 0;
}

static int expr_match_operator(ExprParser *parser, const char *operator_text)
{
    size_t operator_length;

    expr_skip_ws(parser);
    operator_length = strlen(operator_text);

    if (strncmp(parser->text + parser->index, operator_text, operator_length) != 0)
    {
        return 0;
    }

    if ((strcmp(operator_text, "eq") == 0 ||
         strcmp(operator_text, "ne") == 0 ||
         strcmp(operator_text, "in") == 0 ||
         strcmp(operator_text, "ni") == 0) &&
        parser->index + operator_length < parser->length &&
        (isalnum((unsigned char)parser->text[parser->index + operator_length]) ||
         parser->text[parser->index + operator_length] == '_'))
    {
        return 0;
    }

    parser->index += operator_length;
    return 1;
}

static int expr_match_single_operator(ExprParser *parser, char operator_char, char doubled_operator)
{
    expr_skip_ws(parser);
    if (parser->index >= parser->length || parser->text[parser->index] != operator_char)
    {
        return 0;
    }

    if (parser->index + 1 < parser->length && parser->text[parser->index + 1] == doubled_operator)
    {
        return 0;
    }

    parser->index++;
    return 1;
}

static int expr_parse_expression(ExprParser *parser);

static int expr_is_name_char(char character)
{
    return isalnum((unsigned char)character) || character == '_';
}

static int expr_is_simple_name_start(char character)
{
    return isalpha((unsigned char)character) || character == '_';
}

static size_t expr_scan_simple_name(const char *text, size_t length, size_t start)
{
    size_t index = start;

    if (index >= length || !expr_is_simple_name_start(text[index]))
    {
        return 0;
    }

    index++;
    while (index < length && expr_is_name_char(text[index]))
    {
        index++;
    }

    return index - start;
}

static size_t expr_scan_namespace_name(const char *text, size_t length, size_t start)
{
    size_t index = start;

    while (index < length)
    {
        if (expr_is_name_char(text[index]))
        {
            index++;
            continue;
        }

        if (text[index] == ':')
        {
            size_t colon_start = index;
            while (index < length && text[index] == ':')
            {
                index++;
            }

            if (index - colon_start < 2)
            {
                index = colon_start;
                break;
            }
            continue;
        }

        break;
    }

    return index - start;
}

static int expr_parse_variable(ExprParser *parser)
{
    size_t name_start;
    size_t name_length;

    parser->index++;

    if (parser->index < parser->length && parser->text[parser->index] == '{')
    {
        parser->index++;
        if (parser->index < parser->length && parser->text[parser->index] == '}')
        {
            return expr_error(parser, "invalid variable reference");
        }

        while (parser->index < parser->length && parser->text[parser->index] != '}')
        {
            parser->index++;
        }

        if (parser->index >= parser->length)
        {
            return expr_error(parser, "unterminated variable reference");
        }

        parser->index++;
        return 1;
    }

    name_start = parser->index;
    name_length = expr_scan_namespace_name(parser->text, parser->length, parser->index);
    parser->index += name_length;

    if (parser->index < parser->length && parser->text[parser->index] == '(')
    {
        int depth = 1;
        parser->index++;

        while (parser->index < parser->length)
        {
            if (parser->text[parser->index] == '(')
            {
                depth++;
            }
            else if (parser->text[parser->index] == ')')
            {
                depth--;
                if (depth == 0)
                {
                    parser->index++;
                    return 1;
                }
            }
            parser->index++;
        }

        return expr_error(parser, "unterminated array variable reference");
    }

    if (name_length == 0)
    {
        parser->index = name_start;
        return expr_error(parser, "invalid variable reference");
    }

    return 1;
}

static int expr_parse_quoted(ExprParser *parser)
{
    size_t start = parser->index + 1;
    size_t end;
    char *content;
    int result;

    parser->index++;
    while (parser->index < parser->length)
    {
        if (parser->text[parser->index] == '\\' && parser->index + 1 < parser->length)
        {
            parser->index += 2;
            continue;
        }

        if (parser->text[parser->index] == '"')
        {
            end = parser->index;
            content = validator_copy_substring(parser->text + start, end - start);
            if (!content)
            {
                return expr_error(parser, "out of memory");
            }
            result = validator_validate_command_substitutions_in_text(
                parser->context,
                content,
                parser->line,
                parser->column,
                parser->error);
            free(content);
            parser->index++;
            return result;
        }

        parser->index++;
    }

    return expr_error(parser, "unterminated quoted string");
}

static int expr_parse_braced(ExprParser *parser)
{
    int depth = 1;

    parser->index++;
    while (parser->index < parser->length)
    {
        if (parser->text[parser->index] == '{')
        {
            depth++;
        }
        else if (parser->text[parser->index] == '}')
        {
            depth--;
            if (depth == 0)
            {
                parser->index++;
                return 1;
            }
        }
        parser->index++;
    }

    return expr_error(parser, "unterminated braced string");
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

static int expr_parse_identifier_or_call(ExprParser *parser)
{
    size_t start = parser->index;
    size_t name_length;
    size_t name_index;
    size_t keyword_index;
    char lower_name[32];
    static const char *const bool_keywords[] = {
        "true",
        "false",
        "yes",
        "no",
        "on",
        "off"};
    int bool_matches = 0;

    name_length = expr_scan_simple_name(parser->text, parser->length, parser->index);
    if (name_length == 0)
    {
        return expr_error(parser, "invalid expression");
    }
    parser->index += name_length;

    expr_skip_ws(parser);
    if (!expr_match_char(parser, '('))
    {
        if (name_length < sizeof(lower_name))
        {
            for (name_index = 0; name_index < name_length; name_index++)
            {
                lower_name[name_index] = (char)tolower((unsigned char)parser->text[start + name_index]);
            }
            lower_name[name_length] = '\0';

            if (strcmp(lower_name, "inf") == 0 ||
                strcmp(lower_name, "infinity") == 0 ||
                strcmp(lower_name, "nan") == 0 ||
                strcmp(lower_name, "cmdsubst") == 0)
            {
                return 1;
            }

            for (keyword_index = 0; keyword_index < (sizeof(bool_keywords) / sizeof(bool_keywords[0])); keyword_index++)
            {
                if (strncmp(bool_keywords[keyword_index], lower_name, name_length) == 0)
                {
                    bool_matches++;
                }
            }

            if (bool_matches == 1)
            {
                return 1;
            }
        }

        return expr_error(parser, "invalid bareword in expression");
    }

    expr_skip_ws(parser);
    if (expr_match_char(parser, ')'))
    {
        return 1;
    }

    while (1)
    {
        if (!expr_parse_expression(parser))
        {
            return 0;
        }

        expr_skip_ws(parser);
        if (expr_match_char(parser, ')'))
        {
            return 1;
        }

        if (!expr_match_char(parser, ','))
        {
            return expr_error(parser, "expected ',' or ')' in function call");
        }
    }
}

static int expr_parse_number(ExprParser *parser)
{
    size_t start = parser->index;
    int saw_digit_before_dot = 0;
    int saw_digit_after_dot = 0;

    if (parser->text[parser->index] == '0' && parser->index + 1 < parser->length)
    {
        char prefix = parser->text[parser->index + 1];
        int base = 10;
        int found_digit = 0;

        if (prefix == 'x' || prefix == 'X')
        {
            base = 16;
        }
        else if (prefix == 'b' || prefix == 'B')
        {
            base = 2;
        }
        else if (prefix == 'o' || prefix == 'O')
        {
            base = 8;
        }

        if (base != 10)
        {
            parser->index += 2;
            while (parser->index < parser->length)
            {
                char character = parser->text[parser->index];
                int is_valid = 0;

                if (base == 16)
                {
                    is_valid = isxdigit((unsigned char)character);
                }
                else if (base == 2)
                {
                    is_valid = (character == '0' || character == '1');
                }
                else
                {
                    is_valid = (character >= '0' && character <= '7');
                }

                if (!is_valid)
                {
                    break;
                }

                found_digit = 1;
                parser->index++;
            }

            if (!found_digit)
            {
                parser->index = start;
                return expr_error(parser, "invalid number");
            }

            return 1;
        }
    }

    if (parser->text[parser->index] == '0' &&
        parser->index + 1 < parser->length &&
        isdigit((unsigned char)parser->text[parser->index + 1]))
    {
        size_t integer_end = parser->index + 1;
        int invalid_octal = 0;

        while (integer_end < parser->length && isdigit((unsigned char)parser->text[integer_end]))
        {
            if (parser->text[integer_end] >= '8')
            {
                invalid_octal = 1;
            }
            integer_end++;
        }

        if (integer_end >= parser->length ||
            (parser->text[integer_end] != '.' && parser->text[integer_end] != 'e' && parser->text[integer_end] != 'E'))
        {
            if (invalid_octal)
            {
                parser->index = start;
                return expr_error(parser, "invalid number");
            }

            parser->index = integer_end;
            return 1;
        }
    }

    while (parser->index < parser->length && isdigit((unsigned char)parser->text[parser->index]))
    {
        saw_digit_before_dot = 1;
        parser->index++;
    }

    if (parser->index < parser->length && parser->text[parser->index] == '.')
    {
        parser->index++;
        while (parser->index < parser->length && isdigit((unsigned char)parser->text[parser->index]))
        {
            saw_digit_after_dot = 1;
            parser->index++;
        }
    }

    if (!saw_digit_before_dot && !saw_digit_after_dot)
    {
        parser->index = start;
        return expr_error(parser, "invalid number");
    }

    if (parser->index < parser->length &&
        (parser->text[parser->index] == 'e' || parser->text[parser->index] == 'E'))
    {
        size_t exponent_start = parser->index;
        int saw_exponent_digit = 0;

        parser->index++;
        if (parser->index < parser->length &&
            (parser->text[parser->index] == '+' || parser->text[parser->index] == '-'))
        {
            parser->index++;
        }

        while (parser->index < parser->length && isdigit((unsigned char)parser->text[parser->index]))
        {
            saw_exponent_digit = 1;
            parser->index++;
        }

        if (!saw_exponent_digit)
        {
            parser->index = exponent_start;
            return expr_error(parser, "invalid number");
        }
    }

    return 1;
}

static int expr_parse_primary(ExprParser *parser)
{
    expr_skip_ws(parser);
    if (parser->index >= parser->length)
    {
        return expr_error(parser, "invalid expression");
    }

    if (expr_match_char(parser, '('))
    {
        if (!expr_parse_expression(parser))
        {
            return 0;
        }
        if (!expr_match_char(parser, ')'))
        {
            return expr_error(parser, "expected ')' in expression");
        }
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

    if (parser->text[parser->index] == '"')
    {
        return expr_parse_quoted(parser);
    }

    if (parser->text[parser->index] == '{')
    {
        return expr_parse_braced(parser);
    }

    /* Handle runtime concatenation of numeric prefix with variable or command substitution,
     * e.g. 0x$s, 0b$flag, 0o$perm, 0x[hexValue] */
    if (parser->text[parser->index] == '0' && parser->index + 2 < parser->length)
    {
        char prefix = parser->text[parser->index + 1];
        if ((prefix == 'x' || prefix == 'X' || prefix == 'b' || prefix == 'B' || prefix == 'o' || prefix == 'O'))
        {
            char next = parser->text[parser->index + 2];
            if (next == '$')
            {
                parser->index += 2;
                return expr_parse_variable(parser);
            }
            if (next == '[')
            {
                parser->index += 2;
                return expr_parse_command_substitution(parser);
            }
        }
    }

    if (isdigit((unsigned char)parser->text[parser->index]) ||
        (parser->text[parser->index] == '.' &&
         parser->index + 1 < parser->length &&
         isdigit((unsigned char)parser->text[parser->index + 1])))
    {
        return expr_parse_number(parser);
    }

    if (expr_is_simple_name_start(parser->text[parser->index]))
    {
        return expr_parse_identifier_or_call(parser);
    }

    return expr_error(parser, "invalid expression");
}

static int expr_parse_unary(ExprParser *parser)
{
    if (expr_match_operator(parser, "!") ||
        expr_match_operator(parser, "~") ||
        expr_match_operator(parser, "-") ||
        expr_match_operator(parser, "+"))
    {
        return expr_parse_unary(parser);
    }

    return expr_parse_primary(parser);
}

static int expr_parse_power(ExprParser *parser)
{
    if (!expr_parse_unary(parser))
    {
        return 0;
    }

    if (expr_match_operator(parser, "**"))
    {
        if (!expr_parse_power(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_multiplicative(ExprParser *parser)
{
    if (!expr_parse_power(parser))
    {
        return 0;
    }

    while (expr_match_single_operator(parser, '*', '*') ||
           expr_match_operator(parser, "/") ||
           expr_match_operator(parser, "%"))
    {
        if (!expr_parse_power(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_additive(ExprParser *parser)
{
    if (!expr_parse_multiplicative(parser))
    {
        return 0;
    }

    while (expr_match_operator(parser, "+") || expr_match_operator(parser, "-"))
    {
        if (!expr_parse_multiplicative(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_shift(ExprParser *parser)
{
    if (!expr_parse_additive(parser))
    {
        return 0;
    }

    while (expr_match_operator(parser, "<<") || expr_match_operator(parser, ">>"))
    {
        if (!expr_parse_additive(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_relational(ExprParser *parser)
{
    if (!expr_parse_shift(parser))
    {
        return 0;
    }

    while (expr_match_operator(parser, "<=") ||
           expr_match_operator(parser, ">=") ||
           expr_match_operator(parser, "<") ||
           expr_match_operator(parser, ">") ||
           expr_match_operator(parser, "in") ||
           expr_match_operator(parser, "ni"))
    {
        if (!expr_parse_shift(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_equality(ExprParser *parser)
{
    if (!expr_parse_relational(parser))
    {
        return 0;
    }

    while (expr_match_operator(parser, "==") ||
           expr_match_operator(parser, "!=") ||
           expr_match_operator(parser, "eq") ||
           expr_match_operator(parser, "ne"))
    {
        if (!expr_parse_relational(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_bitwise_and(ExprParser *parser)
{
    if (!expr_parse_equality(parser))
    {
        return 0;
    }

    while (expr_match_single_operator(parser, '&', '&'))
    {
        if (!expr_parse_equality(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_bitwise_xor(ExprParser *parser)
{
    if (!expr_parse_bitwise_and(parser))
    {
        return 0;
    }

    while (expr_match_operator(parser, "^"))
    {
        if (!expr_parse_bitwise_and(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_bitwise_or(ExprParser *parser)
{
    if (!expr_parse_bitwise_xor(parser))
    {
        return 0;
    }

    while (expr_match_single_operator(parser, '|', '|'))
    {
        if (!expr_parse_bitwise_xor(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_logical_and(ExprParser *parser)
{
    if (!expr_parse_bitwise_or(parser))
    {
        return 0;
    }

    while (expr_match_operator(parser, "&&"))
    {
        if (!expr_parse_bitwise_or(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_logical_or(ExprParser *parser)
{
    if (!expr_parse_logical_and(parser))
    {
        return 0;
    }

    while (expr_match_operator(parser, "||"))
    {
        if (!expr_parse_logical_and(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_ternary(ExprParser *parser)
{
    if (!expr_parse_logical_or(parser))
    {
        return 0;
    }

    if (expr_match_char(parser, '?'))
    {
        if (!expr_parse_expression(parser))
        {
            return 0;
        }
        if (!expr_match_char(parser, ':'))
        {
            return expr_error(parser, "expected ':' in ternary expression");
        }
        if (!expr_parse_expression(parser))
        {
            return 0;
        }
    }

    return 1;
}

static int expr_parse_expression(ExprParser *parser)
{
    return expr_parse_ternary(parser);
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
