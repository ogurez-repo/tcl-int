#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    return *endptr == '\0';
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

static int validate_var_name_text(ExprEval *eval, const char *name, size_t length)
{
    size_t i;

    if (length == 0 || !is_var_start_char(name[0]))
    {
        return expr_error(eval, "invalid variable reference");
    }

    for (i = 1; i < length; i++)
    {
        if (!is_var_char(name[i]))
        {
            return expr_semantic_error(eval, "array/namespace variables are not supported");
        }
    }

    return 1;
}

static int expr_parse_expression(ExprEval *eval, long long *out, int evaluate);

static int expr_parse_variable(ExprEval *eval, long long *out, int evaluate)
{
    eval->index++;

    if (eval->index >= eval->length)
    {
        return expr_error(eval, "invalid variable reference");
    }

    if (eval->text[eval->index] == '{')
    {
        size_t start;
        size_t length;

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

        length = eval->index - start;
        if (!validate_var_name_text(eval, eval->text + start, length))
        {
            return 0;
        }

        if (evaluate)
        {
            char *name = (char *)malloc(length + 1);
            const char *value;

            if (!name)
            {
                return expr_semantic_error(eval, "out of memory");
            }

            memcpy(name, eval->text + start, length);
            name[length] = '\0';
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
        }
        else
        {
            *out = 0;
        }

        eval->index++;
        return 1;
    }
    else
    {
        size_t start = eval->index;
        size_t length = scan_variable_name(eval->text, eval->length, eval->index);

        if (length == 0)
        {
            return expr_error(eval, "invalid variable reference");
        }

        eval->index += length;
        if (eval->index < eval->length && eval->text[eval->index] == '(')
        {
            return expr_semantic_error(eval, "array/namespace variables are not supported");
        }

        if (!validate_var_name_text(eval, eval->text + start, length))
        {
            return 0;
        }

        if (evaluate)
        {
            char *name = (char *)malloc(length + 1);
            const char *value;

            if (!name)
            {
                return expr_semantic_error(eval, "out of memory");
            }

            memcpy(name, eval->text + start, length);
            name[length] = '\0';
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
        }
        else
        {
            *out = 0;
        }

        return 1;
    }
}

static int expr_parse_number(ExprEval *eval, long long *out, int evaluate)
{
    size_t start;
    size_t len;

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

    if (!evaluate)
    {
        *out = 0;
        return 1;
    }

    len = eval->index - start;
    {
        char *number = (char *)malloc(len + 1);
        int ok;

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
}

static int expr_parse_command_substitution(ExprEval *eval, long long *out, int evaluate)
{
    size_t end;

    if (!validator_find_matching_bracket(eval->text, eval->index, &end))
    {
        return expr_error(eval, "unterminated command substitution");
    }

    if (!evaluate)
    {
        eval->index = end + 1;
        *out = 0;
        return 1;
    }

    {
        char *text_result = NULL;

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
}

static int expr_parse_primary(ExprEval *eval, long long *out, int evaluate)
{
    expr_skip_ws(eval);

    if (eval->index >= eval->length)
    {
        return expr_error(eval, "invalid expression");
    }

    if (expr_parse_number(eval, out, evaluate))
    {
        return 1;
    }

    if (eval->text[eval->index] == '$')
    {
        return expr_parse_variable(eval, out, evaluate);
    }

    if (eval->text[eval->index] == '[')
    {
        return expr_parse_command_substitution(eval, out, evaluate);
    }

    if (expr_match_char(eval, '('))
    {
        if (!expr_parse_expression(eval, out, evaluate))
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

static int expr_parse_unary(ExprEval *eval, long long *out, int evaluate)
{
    expr_skip_ws(eval);

    if (expr_match_char(eval, '+'))
    {
        return expr_parse_unary(eval, out, evaluate);
    }

    if (expr_match_char(eval, '-'))
    {
        if (!expr_parse_unary(eval, out, evaluate))
        {
            return 0;
        }

        if (evaluate)
        {
            *out = -*out;
        }
        else
        {
            *out = 0;
        }

        return 1;
    }

    if (expr_match_char(eval, '!'))
    {
        if (!expr_parse_unary(eval, out, evaluate))
        {
            return 0;
        }

        if (evaluate)
        {
            *out = (*out == 0) ? 1 : 0;
        }
        else
        {
            *out = 0;
        }

        return 1;
    }

    return expr_parse_primary(eval, out, evaluate);
}

static int expr_parse_multiplicative(ExprEval *eval, long long *out, int evaluate)
{
    if (!expr_parse_unary(eval, out, evaluate))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (expr_match_char(eval, '*'))
        {
            if (!expr_parse_unary(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out) * right;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        if (expr_match_char(eval, '/'))
        {
            if (!expr_parse_unary(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                if (right == 0)
                {
                    return expr_semantic_error(eval, "Division by zero");
                }
                *out = (*out) / right;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        if (expr_match_char(eval, '%'))
        {
            if (!expr_parse_unary(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                if (right == 0)
                {
                    return expr_semantic_error(eval, "Division by zero");
                }
                *out = (*out) % right;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_additive(ExprEval *eval, long long *out, int evaluate)
{
    if (!expr_parse_multiplicative(eval, out, evaluate))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (expr_match_char(eval, '+'))
        {
            if (!expr_parse_multiplicative(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out) + right;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        if (expr_match_char(eval, '-'))
        {
            if (!expr_parse_multiplicative(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out) - right;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_relational(ExprEval *eval, long long *out, int evaluate)
{
    if (!expr_parse_additive(eval, out, evaluate))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (expr_match_text(eval, "<="))
        {
            if (!expr_parse_additive(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out <= right) ? 1 : 0;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        if (expr_match_text(eval, ">="))
        {
            if (!expr_parse_additive(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out >= right) ? 1 : 0;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        if (expr_match_char(eval, '<'))
        {
            if (!expr_parse_additive(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out < right) ? 1 : 0;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        if (expr_match_char(eval, '>'))
        {
            if (!expr_parse_additive(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out > right) ? 1 : 0;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_equality(ExprEval *eval, long long *out, int evaluate)
{
    if (!expr_parse_relational(eval, out, evaluate))
    {
        return 0;
    }

    while (1)
    {
        long long right;

        if (expr_match_text(eval, "=="))
        {
            if (!expr_parse_relational(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out == right) ? 1 : 0;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        if (expr_match_text(eval, "!="))
        {
            if (!expr_parse_relational(eval, &right, evaluate))
            {
                return 0;
            }

            if (evaluate)
            {
                *out = (*out != right) ? 1 : 0;
            }
            else
            {
                *out = 0;
            }
            continue;
        }

        return 1;
    }
}

static int expr_parse_logical_and(ExprEval *eval, long long *out, int evaluate)
{
    if (!expr_parse_equality(eval, out, evaluate))
    {
        return 0;
    }

    while (1)
    {
        long long right = 0;

        if (!expr_match_text(eval, "&&"))
        {
            return 1;
        }

        if (!evaluate)
        {
            if (!expr_parse_equality(eval, &right, 0))
            {
                return 0;
            }
            *out = 0;
            continue;
        }

        if (*out == 0)
        {
            if (!expr_parse_equality(eval, &right, 0))
            {
                return 0;
            }
            *out = 0;
            continue;
        }

        if (!expr_parse_equality(eval, &right, 1))
        {
            return 0;
        }

        *out = (right != 0) ? 1 : 0;
    }
}

static int expr_parse_expression(ExprEval *eval, long long *out, int evaluate)
{
    if (!expr_parse_logical_and(eval, out, evaluate))
    {
        return 0;
    }

    while (1)
    {
        long long right = 0;

        if (!expr_match_text(eval, "||"))
        {
            return 1;
        }

        if (!evaluate)
        {
            if (!expr_parse_logical_and(eval, &right, 0))
            {
                return 0;
            }
            *out = 0;
            continue;
        }

        if (*out != 0)
        {
            if (!expr_parse_logical_and(eval, &right, 0))
            {
                return 0;
            }
            *out = 1;
            continue;
        }

        if (!expr_parse_logical_and(eval, &right, 1))
        {
            return 0;
        }

        *out = (right != 0) ? 1 : 0;
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

    if (!expr_parse_expression(&eval, &value, 1))
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
