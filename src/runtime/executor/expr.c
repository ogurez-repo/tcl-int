#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/errors.h"
#include "runtime/executor.h"
#include "runtime/executor/internal.h"
#include "runtime/validator.h"
#include "runtime/variables.h"

typedef struct ExprEval
{
    const char *text;
    size_t length;
    size_t index;
    int line;
    int column;
    ExecutorContext *context;
    TclError *error;
} ExprEval;

static int is_var_start_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_var_char(char c)
{
    return is_var_start_char(c) || (c >= '0' && c <= '9');
}

int expr_to_longlong(const char *str, long long *out)
{
    char *endptr;

    if (!str || *str == '\0')
    {
        return 0;
    }

    if (str[0] == '+' || str[0] == '-')
    {
        if (!isdigit((unsigned char)str[1]))
        {
            return 0;
        }
    }
    else if (!isdigit((unsigned char)str[0]))
    {
        return 0;
    }

    if (str[0] == '0' && isdigit((unsigned char)str[1]))
    {
        size_t i;
        for (i = 1; str[i]; i++)
        {
            if (!isdigit((unsigned char)str[i]))
            {
                return 0;
            }
        }
    }

    *out = strtoll(str, &endptr, 10);
    if (*endptr != '\0')
    {
        return 0;
    }

    return 1;
}

static void expr_skip_ws(ExprEval *eval)
{
    while (eval->index < eval->length && isspace((unsigned char)eval->text[eval->index]))
    {
        eval->index++;
    }
}

static int expr_error(ExprEval *eval, const char *message)
{
    tcl_error_set(eval->error, TCL_ERROR_SYNTAX, eval->line, eval->column, message);
    return 0;
}

static int expr_semantic_error(ExprEval *eval, const char *message)
{
    tcl_error_set(eval->error, TCL_ERROR_SEMANTIC, eval->line, eval->column, message);
    return 0;
}

static int expr_match_char(ExprEval *eval, char c)
{
    expr_skip_ws(eval);
    if (eval->index < eval->length && eval->text[eval->index] == c)
    {
        eval->index++;
        return 1;
    }

    return 0;
}

static int expr_match_text(ExprEval *eval, const char *text)
{
    size_t len = strlen(text);

    expr_skip_ws(eval);
    if (eval->index + len > eval->length)
    {
        return 0;
    }

    if (strncmp(eval->text + eval->index, text, len) != 0)
    {
        return 0;
    }

    eval->index += len;
    return 1;
}

static int expr_parse_expression(ExprEval *eval, long long *out);

static int parse_integer_string(ExprEval *eval, const char *value, long long *out)
{
    if (!expr_to_longlong(value, out))
    {
        char message[256];
        snprintf(message, sizeof(message), "Expected integer, got \"%s\"", value);
        return expr_semantic_error(eval, message);
    }

    return 1;
}

static int validate_var_name(ExprEval *eval, const char *name)
{
    size_t index;

    if (!name || name[0] == '\0' || !is_var_start_char(name[0]))
    {
        return expr_error(eval, "invalid variable reference");
    }

    for (index = 1; name[index]; index++)
    {
        if (!is_var_char(name[index]))
        {
            return expr_semantic_error(eval, "array/namespace variables are not supported");
        }
    }

    return 1;
}

static int expr_parse_variable(ExprEval *eval, long long *out)
{
    char *name;
    const char *value;

    eval->index++;

    if (eval->index >= eval->length)
    {
        return expr_error(eval, "invalid variable reference");
    }

    if (eval->text[eval->index] == '{')
    {
        size_t start;
        size_t len;

        eval->index++;
        start = eval->index;
        while (eval->index < eval->length && eval->text[eval->index] != '}')
        {
            eval->index++;
        }

        if (eval->index >= eval->length)
        {
            return expr_error(eval, "unterminated variable reference");
        }

        len = eval->index - start;
        name = (char *)malloc(len + 1);
        if (!name)
        {
            return expr_semantic_error(eval, "out of memory");
        }

        memcpy(name, eval->text + start, len);
        name[len] = '\0';
        eval->index++;
    }
    else
    {
        size_t start = eval->index;
        size_t len = scan_variable_name(eval->text, eval->length, eval->index);

        if (len == 0)
        {
            return expr_error(eval, "invalid variable reference");
        }

        eval->index += len;
        if (eval->index < eval->length && eval->text[eval->index] == '(')
        {
            return expr_semantic_error(eval, "array/namespace variables are not supported");
        }

        name = (char *)malloc(len + 1);
        if (!name)
        {
            return expr_semantic_error(eval, "out of memory");
        }

        memcpy(name, eval->text + start, len);
        name[len] = '\0';
    }

    if (!validate_var_name(eval, name))
    {
        free(name);
        return 0;
    }

    value = variables_get(eval->context->variables, name);
    if (!value)
    {
        tcl_error_setf(eval->error, TCL_ERROR_SEMANTIC, eval->line, eval->column, "Undefined variable: %s", name);
        free(name);
        return 0;
    }

    if (!parse_integer_string(eval, value, out))
    {
        free(name);
        return 0;
    }

    free(name);
    return 1;
}

static int expr_parse_number(ExprEval *eval, long long *out)
{
    size_t start;
    size_t len;
    char *number;
    int ok;

    expr_skip_ws(eval);
    start = eval->index;

    while (eval->index < eval->length && isdigit((unsigned char)eval->text[eval->index]))
    {
        eval->index++;
    }

    if (eval->index == start)
    {
        return 0;
    }

    len = eval->index - start;
    number = (char *)malloc(len + 1);
    if (!number)
    {
        return expr_semantic_error(eval, "out of memory");
    }

    memcpy(number, eval->text + start, len);
    number[len] = '\0';

    ok = parse_integer_string(eval, number, out);
    free(number);
    return ok;
}

static int expr_parse_command_substitution(ExprEval *eval, long long *out)
{
    size_t end;
    char *text_result = NULL;

    if (!validator_find_matching_bracket(eval->text, eval->index, &end))
    {
        return expr_error(eval, "unterminated command substitution");
    }

    if (!execute_command_substitution(
            eval->context,
            eval->text,
            eval->length,
            eval->index,
            eval->line,
            eval->column,
            &end,
            eval->error,
            &text_result))
    {
        return 0;
    }

    eval->index = end + 1;

    if (!parse_integer_string(eval, text_result, out))
    {
        free(text_result);
        return 0;
    }

    free(text_result);
    return 1;
}

static int expr_parse_primary(ExprEval *eval, long long *out)
{
    expr_skip_ws(eval);

    if (eval->index >= eval->length)
    {
        return expr_error(eval, "invalid expression");
    }

    if (expr_parse_number(eval, out))
    {
        return 1;
    }

    if (eval->text[eval->index] == '$')
    {
        return expr_parse_variable(eval, out);
    }

    if (eval->text[eval->index] == '[')
    {
        return expr_parse_command_substitution(eval, out);
    }

    if (expr_match_char(eval, '('))
    {
        if (!expr_parse_expression(eval, out))
        {
            return 0;
        }

        if (!expr_match_char(eval, ')'))
        {
            return expr_error(eval, "missing ')' in expression");
        }

        return 1;
    }

    return expr_error(eval, "invalid expression");
}

static int expr_parse_unary(ExprEval *eval, long long *out)
{
    expr_skip_ws(eval);

    if (expr_match_char(eval, '+'))
    {
        return expr_parse_unary(eval, out);
    }

    if (expr_match_char(eval, '-'))
    {
        if (!expr_parse_unary(eval, out))
        {
            return 0;
        }
        *out = -*out;
        return 1;
    }

    if (expr_match_char(eval, '!'))
    {
        if (!expr_parse_unary(eval, out))
        {
            return 0;
        }
        *out = (*out == 0) ? 1 : 0;
        return 1;
    }

    return expr_parse_primary(eval, out);
}

static int expr_parse_multiplicative(ExprEval *eval, long long *out)
{
    if (!expr_parse_unary(eval, out))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (expr_match_char(eval, '*'))
        {
            if (!expr_parse_unary(eval, &right))
            {
                return 0;
            }
            *out = (*out) * right;
            continue;
        }

        if (expr_match_char(eval, '/'))
        {
            if (!expr_parse_unary(eval, &right))
            {
                return 0;
            }
            if (right == 0)
            {
                return expr_semantic_error(eval, "Division by zero");
            }
            *out = (*out) / right;
            continue;
        }

        if (expr_match_char(eval, '%'))
        {
            if (!expr_parse_unary(eval, &right))
            {
                return 0;
            }
            if (right == 0)
            {
                return expr_semantic_error(eval, "Division by zero");
            }
            *out = (*out) % right;
            continue;
        }

        return 1;
    }
}

static int expr_parse_additive(ExprEval *eval, long long *out)
{
    if (!expr_parse_multiplicative(eval, out))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (expr_match_char(eval, '+'))
        {
            if (!expr_parse_multiplicative(eval, &right))
            {
                return 0;
            }
            *out = (*out) + right;
            continue;
        }

        if (expr_match_char(eval, '-'))
        {
            if (!expr_parse_multiplicative(eval, &right))
            {
                return 0;
            }
            *out = (*out) - right;
            continue;
        }

        return 1;
    }
}

static int expr_parse_relational(ExprEval *eval, long long *out)
{
    if (!expr_parse_additive(eval, out))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (expr_match_text(eval, "<="))
        {
            if (!expr_parse_additive(eval, &right))
            {
                return 0;
            }
            *out = (*out <= right) ? 1 : 0;
            continue;
        }

        if (expr_match_text(eval, ">="))
        {
            if (!expr_parse_additive(eval, &right))
            {
                return 0;
            }
            *out = (*out >= right) ? 1 : 0;
            continue;
        }

        if (expr_match_char(eval, '<'))
        {
            if (!expr_parse_additive(eval, &right))
            {
                return 0;
            }
            *out = (*out < right) ? 1 : 0;
            continue;
        }

        if (expr_match_char(eval, '>'))
        {
            if (!expr_parse_additive(eval, &right))
            {
                return 0;
            }
            *out = (*out > right) ? 1 : 0;
            continue;
        }

        return 1;
    }
}

static int expr_parse_equality(ExprEval *eval, long long *out)
{
    if (!expr_parse_relational(eval, out))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (expr_match_text(eval, "=="))
        {
            if (!expr_parse_relational(eval, &right))
            {
                return 0;
            }
            *out = (*out == right) ? 1 : 0;
            continue;
        }

        if (expr_match_text(eval, "!="))
        {
            if (!expr_parse_relational(eval, &right))
            {
                return 0;
            }
            *out = (*out != right) ? 1 : 0;
            continue;
        }

        return 1;
    }
}

static int expr_parse_logical_and(ExprEval *eval, long long *out)
{
    if (!expr_parse_equality(eval, out))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (!expr_match_text(eval, "&&"))
        {
            return 1;
        }

        if (!expr_parse_equality(eval, &right))
        {
            return 0;
        }

        *out = ((*out != 0) && (right != 0)) ? 1 : 0;
    }
}

static int expr_parse_expression(ExprEval *eval, long long *out)
{
    if (!expr_parse_logical_and(eval, out))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (!expr_match_text(eval, "||"))
        {
            return 1;
        }

        if (!expr_parse_logical_and(eval, &right))
        {
            return 0;
        }

        *out = ((*out != 0) || (right != 0)) ? 1 : 0;
    }
}

int evaluate_expression(
    ExecutorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error,
    char **result)
{
    ExprEval eval;
    long long value;
    char buffer[64];

    eval.text = text;
    eval.length = strlen(text);
    eval.index = 0;
    eval.line = line;
    eval.column = column;
    eval.context = context;
    eval.error = error;

    if (!expr_parse_expression(&eval, &value))
    {
        return 0;
    }

    expr_skip_ws(&eval);
    if (eval.index != eval.length)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, line, column, "unexpected token in expression");
        return 0;
    }

    snprintf(buffer, sizeof(buffer), "%lld", value);
    *result = (char *)malloc(strlen(buffer) + 1);
    if (!*result)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
        return 0;
    }

    strcpy(*result, buffer);
    return 1;
}
