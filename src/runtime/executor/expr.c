#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/executor.h"
#include "runtime/executor/internal.h"
#include "runtime/variables.h"
#include "runtime/validator.h"
#include "core/errors.h"
#include "core/script_parser.h"

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

typedef struct ExprValue
{
    char *str;
} ExprValue;

static void expr_value_set(ExprValue *val, const char *str)
{
    free(val->str);
    val->str = (char *)malloc(strlen(str) + 1);
    if (val->str)
    {
        strcpy(val->str, str);
    }
}

static void expr_value_clear(ExprValue *val)
{
    free(val->str);
    val->str = NULL;
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

static int expr_match_char(ExprEval *eval, char character)
{
    expr_skip_ws(eval);
    if (eval->index < eval->length && eval->text[eval->index] == character)
    {
        eval->index++;
        return 1;
    }
    return 0;
}

static int expr_match_operator(ExprEval *eval, const char *operator_text)
{
    size_t operator_length;

    expr_skip_ws(eval);
    operator_length = strlen(operator_text);

    if (strncmp(eval->text + eval->index, operator_text, operator_length) != 0)
    {
        return 0;
    }

    if ((strcmp(operator_text, "eq") == 0 ||
         strcmp(operator_text, "ne") == 0 ||
         strcmp(operator_text, "in") == 0 ||
         strcmp(operator_text, "ni") == 0) &&
        eval->index + operator_length < eval->length &&
        (isalnum((unsigned char)eval->text[eval->index + operator_length]) ||
         eval->text[eval->index + operator_length] == '_'))
    {
        return 0;
    }

    eval->index += operator_length;
    return 1;
}

static int expr_match_single_operator(ExprEval *eval, char operator_char, char doubled_operator)
{
    expr_skip_ws(eval);
    if (eval->index >= eval->length || eval->text[eval->index] != operator_char)
    {
        return 0;
    }

    if (eval->index + 1 < eval->length && eval->text[eval->index + 1] == doubled_operator)
    {
        return 0;
    }

    eval->index++;
    return 1;
}

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

int expr_to_longlong(const char *str, long long *out)
{
    char *endptr;
    long long val;

    if (!str || !*str)
    {
        return 0;
    }

    if (str[0] == '0' && (str[1] == 'b' || str[1] == 'B'))
    {
        val = strtoll(str + 2, &endptr, 2);
        if (*endptr == '\0')
        {
            *out = val;
            return 1;
        }
        return 0;
    }

    if (str[0] == '0' && (str[1] == 'o' || str[1] == 'O'))
    {
        val = strtoll(str + 2, &endptr, 8);
        if (*endptr == '\0')
        {
            *out = val;
            return 1;
        }
        return 0;
    }

    val = strtoll(str, &endptr, 0);
    if (*endptr == '\0')
    {
        *out = val;
        return 1;
    }

    val = (long long)strtod(str, &endptr);
    if (*endptr == '\0')
    {
        *out = val;
        return 1;
    }

    return 0;
}

static void expr_from_longlong(long long val, char *buf, size_t size)
{
    snprintf(buf, size, "%lld", val);
}

static int expr_parse_variable(ExprEval *eval, char **name)
{
    size_t name_start;
    size_t name_length;

    eval->index++;

    if (eval->index < eval->length && eval->text[eval->index] == '{')
    {
        size_t content_start;
        size_t content_length;
        eval->index++;
        content_start = eval->index;
        while (eval->index < eval->length && eval->text[eval->index] != '}')
        {
            eval->index++;
        }
        content_length = eval->index - content_start;
        if (eval->index < eval->length && eval->text[eval->index] == '}')
        {
            eval->index++;
        }

        *name = (char *)malloc(content_length + 1);
        if (!*name)
        {
            return expr_error(eval, "out of memory");
        }
        memcpy(*name, eval->text + content_start, content_length);
        (*name)[content_length] = '\0';
        return 1;
    }

    name_start = eval->index;
    name_length = scan_variable_name(eval->text, eval->length, eval->index);
    eval->index += name_length;

    if (eval->index < eval->length && eval->text[eval->index] == '(')
    {
        size_t base_name_length = name_length;
        int depth = 1;
        size_t idx_start;
        size_t idx_length;
        size_t total_length;

        eval->index++;
        idx_start = eval->index;
        while (eval->index < eval->length)
        {
            if (eval->text[eval->index] == '(')
            {
                depth++;
            }
            else if (eval->text[eval->index] == ')')
            {
                depth--;
                if (depth == 0)
                {
                    break;
                }
            }
            eval->index++;
        }

        idx_length = eval->index - idx_start;
        if (eval->index < eval->length && eval->text[eval->index] == ')')
        {
            eval->index++;
        }

        total_length = base_name_length + idx_length + 3;
        *name = (char *)malloc(total_length);
        if (!*name)
        {
            return expr_error(eval, "out of memory");
        }
        memcpy(*name, eval->text + name_start, base_name_length);
        (*name)[base_name_length] = '(';
        memcpy(*name + base_name_length + 1, eval->text + idx_start, idx_length);
        (*name)[base_name_length + 1 + idx_length] = ')';
        (*name)[total_length - 1] = '\0';
        return 1;
    }

    if (name_length == 0)
    {
        return expr_error(eval, "invalid variable reference");
    }

    *name = (char *)malloc(name_length + 1);
    if (!*name)
    {
        return expr_error(eval, "out of memory");
    }
    memcpy(*name, eval->text + name_start, name_length);
    (*name)[name_length] = '\0';
    return 1;
}

static int expr_parse_number(ExprEval *eval, ExprValue *result)
{
    size_t start = eval->index;
    int saw_digit_before_dot = 0;
    int saw_digit_after_dot = 0;

    if (eval->text[eval->index] == '0' && eval->index + 1 < eval->length)
    {
        char prefix = eval->text[eval->index + 1];
        int base = 10;
        int found_digit = 0;

        if (prefix == 'x' || prefix == 'X')
            base = 16;
        else if (prefix == 'b' || prefix == 'B')
            base = 2;
        else if (prefix == 'o' || prefix == 'O')
            base = 8;

        if (base != 10)
        {
            eval->index += 2;
            while (eval->index < eval->length)
            {
                char character = eval->text[eval->index];
                int is_valid = 0;
                if (base == 16)
                    is_valid = isxdigit((unsigned char)character);
                else if (base == 2)
                    is_valid = (character == '0' || character == '1');
                else
                    is_valid = (character >= '0' && character <= '7');

                if (!is_valid)
                    break;
                found_digit = 1;
                eval->index++;
            }

            if (!found_digit)
            {
                eval->index = start;
                return expr_error(eval, "invalid number");
            }
            goto number_done;
        }
    }

    if (eval->text[eval->index] == '0' &&
        eval->index + 1 < eval->length &&
        isdigit((unsigned char)eval->text[eval->index + 1]))
    {
        size_t integer_end = eval->index + 1;
        int invalid_octal = 0;

        while (integer_end < eval->length && isdigit((unsigned char)eval->text[integer_end]))
        {
            if (eval->text[integer_end] >= '8')
                invalid_octal = 1;
            integer_end++;
        }

        if (integer_end >= eval->length ||
            (eval->text[integer_end] != '.' && eval->text[integer_end] != 'e' && eval->text[integer_end] != 'E'))
        {
            if (invalid_octal)
            {
                eval->index = start;
                return expr_error(eval, "invalid number");
            }
            eval->index = integer_end;
            goto number_done;
        }
    }

    while (eval->index < eval->length && isdigit((unsigned char)eval->text[eval->index]))
    {
        saw_digit_before_dot = 1;
        eval->index++;
    }

    if (eval->index < eval->length && eval->text[eval->index] == '.')
    {
        eval->index++;
        while (eval->index < eval->length && isdigit((unsigned char)eval->text[eval->index]))
        {
            saw_digit_after_dot = 1;
            eval->index++;
        }
    }

    if (!saw_digit_before_dot && !saw_digit_after_dot)
    {
        eval->index = start;
        return expr_error(eval, "invalid number");
    }

    if (eval->index < eval->length &&
        (eval->text[eval->index] == 'e' || eval->text[eval->index] == 'E'))
    {
        size_t exponent_start = eval->index;
        int saw_exponent_digit = 0;

        eval->index++;
        if (eval->index < eval->length &&
            (eval->text[eval->index] == '+' || eval->text[eval->index] == '-'))
        {
            eval->index++;
        }

        while (eval->index < eval->length && isdigit((unsigned char)eval->text[eval->index]))
        {
            saw_exponent_digit = 1;
            eval->index++;
        }

        if (!saw_exponent_digit)
        {
            eval->index = exponent_start;
            return expr_error(eval, "invalid number");
        }
    }

number_done:
    {
        size_t len = eval->index - start;
        char *num_str = (char *)malloc(len + 1);
        if (!num_str)
        {
            return expr_error(eval, "out of memory");
        }
        memcpy(num_str, eval->text + start, len);
        num_str[len] = '\0';
        expr_value_set(result, num_str);
        free(num_str);
    }
    return 1;
}

static int expr_parse_expression(ExprEval *eval, ExprValue *result);

static int expr_parse_identifier_or_call(ExprEval *eval, ExprValue *result)
{
    size_t start = eval->index;
    size_t name_length = expr_scan_simple_name(eval->text, eval->length, eval->index);
    size_t name_index;
    char lower_name[32];

    if (name_length == 0)
        return expr_error(eval, "invalid expression");
    eval->index += name_length;

    expr_skip_ws(eval);
    if (expr_match_char(eval, '('))
    {
        expr_skip_ws(eval);
        if (expr_match_char(eval, ')'))
        {
            expr_value_set(result, "0");
            return 1;
        }
        while (1)
        {
            ExprValue arg; memset(&arg, 0, sizeof(arg));
            if (!expr_parse_expression(eval, &arg))
                return 0;
            expr_value_clear(&arg);
            expr_skip_ws(eval);
            if (expr_match_char(eval, ')'))
                break;
            if (!expr_match_char(eval, ','))
                return expr_error(eval, "expected ',' or ')' in function call");
        }
        expr_value_set(result, "0");
        return 1;
    }

    if (name_length < sizeof(lower_name))
    {
        for (name_index = 0; name_index < name_length; name_index++)
        {
            lower_name[name_index] = (char)tolower((unsigned char)eval->text[start + name_index]);
        }
        lower_name[name_length] = '\0';

        if (strcmp(lower_name, "true") == 0 || strcmp(lower_name, "yes") == 0 || strcmp(lower_name, "on") == 0)
        {
            expr_value_set(result, "1");
            return 1;
        }
        if (strcmp(lower_name, "false") == 0 || strcmp(lower_name, "no") == 0 || strcmp(lower_name, "off") == 0)
        {
            expr_value_set(result, "0");
            return 1;
        }
        if (strcmp(lower_name, "inf") == 0 || strcmp(lower_name, "infinity") == 0 || strcmp(lower_name, "nan") == 0)
        {
            return expr_error(eval, "floating point not supported in integer mode");
        }
    }

    return expr_error(eval, "invalid bareword in expression");
}

static int expr_parse_primary(ExprEval *eval, ExprValue *result)
{
    expr_skip_ws(eval);
    if (eval->index >= eval->length)
        return expr_error(eval, "invalid expression");

    if (expr_match_char(eval, '('))
    {
        if (!expr_parse_expression(eval, result))
            return 0;
        if (!expr_match_char(eval, ')'))
            return expr_error(eval, "expected ')' in expression");
        return 1;
    }

    if (eval->text[eval->index] == '$')
    {
        char *name = NULL;
        const char *value;
        if (!expr_parse_variable(eval, &name))
            return 0;
        value = variables_get(eval->context->variables, name);
        if (!value)
        {
            tcl_error_setf(eval->error, TCL_ERROR_SEMANTIC, eval->line, eval->column, "variable '$%s' not found", name);
            free(name);
            return 0;
        }
        expr_value_set(result, value);
        free(name);
        return 1;
    }

    if (eval->text[eval->index] == '[')
    {
        size_t end;
        char *sub_result;
        if (!validator_find_matching_bracket(eval->text, eval->index, &end))
            return expr_error(eval, "unterminated command substitution");
        if (!execute_command_substitution(eval->context, eval->text, eval->length, eval->index, &end, eval->error, &sub_result))
            return 0;
        expr_value_set(result, sub_result);
        free(sub_result);
        eval->index = end + 1;
        return 1;
    }

    if (eval->text[eval->index] == '"')
    {
        size_t start = eval->index + 1;
        size_t end_pos;
        eval->index++;
        while (eval->index < eval->length)
        {
            if (eval->text[eval->index] == '\\' && eval->index + 1 < eval->length)
            {
                eval->index += 2;
                continue;
            }
            if (eval->text[eval->index] == '"')
            {
                end_pos = eval->index;
                {
                    size_t len = end_pos - start;
                    char *str = (char *)malloc(len + 1);
                    if (!str)
                        return expr_error(eval, "out of memory");
                    memcpy(str, eval->text + start, len);
                    str[len] = '\0';
                    expr_value_set(result, str);
                    free(str);
                }
                eval->index++;
                return 1;
            }
            eval->index++;
        }
        return expr_error(eval, "unterminated quoted string");
    }

    if (eval->text[eval->index] == '{')
    {
        int depth = 1;
        size_t start = eval->index + 1;
        eval->index++;
        while (eval->index < eval->length)
        {
            if (eval->text[eval->index] == '{')
                depth++;
            else if (eval->text[eval->index] == '}')
            {
                depth--;
                if (depth == 0)
                {
                    size_t len = eval->index - start;
                    char *str = (char *)malloc(len + 1);
                    if (!str)
                        return expr_error(eval, "out of memory");
                    memcpy(str, eval->text + start, len);
                    str[len] = '\0';
                    expr_value_set(result, str);
                    free(str);
                    eval->index++;
                    return 1;
                }
            }
            eval->index++;
        }
        return expr_error(eval, "unterminated braced string");
    }

    if (isdigit((unsigned char)eval->text[eval->index]) ||
        (eval->text[eval->index] == '.' && eval->index + 1 < eval->length && isdigit((unsigned char)eval->text[eval->index + 1])))
    {
        return expr_parse_number(eval, result);
    }

    if (expr_is_simple_name_start(eval->text[eval->index]))
    {
        return expr_parse_identifier_or_call(eval, result);
    }

    return expr_error(eval, "invalid expression");
}

static int expr_parse_unary(ExprEval *eval, ExprValue *result)
{
    if (expr_match_operator(eval, "!"))
    {
        ExprValue operand; memset(&operand, 0, sizeof(operand));
        if (!expr_parse_unary(eval, &operand))
            return 0;
        long long val;
        if (!expr_to_longlong(operand.str, &val))
        {
            expr_value_clear(&operand);
            return expr_error(eval, "expected numeric value");
        }
        expr_value_set(result, val ? "0" : "1");
        expr_value_clear(&operand);
        return 1;
    }
    if (expr_match_operator(eval, "~"))
    {
        ExprValue operand; memset(&operand, 0, sizeof(operand));
        if (!expr_parse_unary(eval, &operand))
            return 0;
        long long val;
        if (!expr_to_longlong(operand.str, &val))
        {
            expr_value_clear(&operand);
            return expr_error(eval, "expected numeric value");
        }
        {
            char buf[32];
            expr_from_longlong(~val, buf, sizeof(buf));
            expr_value_set(result, buf);
        }
        expr_value_clear(&operand);
        return 1;
    }
    if (expr_match_operator(eval, "-"))
    {
        ExprValue operand; memset(&operand, 0, sizeof(operand));
        if (!expr_parse_unary(eval, &operand))
            return 0;
        long long val;
        if (!expr_to_longlong(operand.str, &val))
        {
            expr_value_clear(&operand);
            return expr_error(eval, "expected numeric value");
        }
        {
            char buf[32];
            expr_from_longlong(-val, buf, sizeof(buf));
            expr_value_set(result, buf);
        }
        expr_value_clear(&operand);
        return 1;
    }
    if (expr_match_operator(eval, "+"))
    {
        return expr_parse_unary(eval, result);
    }
    return expr_parse_primary(eval, result);
}

static int expr_parse_power(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_unary(eval, &left))
    {
        return 0;
    }

    if (expr_match_operator(eval, "**"))
    {
        ExprValue right; memset(&right, 0, sizeof(right));
        if (!expr_parse_power(eval, &right))
        {
            expr_value_clear(&left);
            return 0;
        }
        long long l, r;
        if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
        {
            expr_value_clear(&left);
            expr_value_clear(&right);
            return expr_error(eval, "expected numeric value");
        }
        {
            long long res = 1;
            long long i;
            char buf[32];
            for (i = 0; i < r; i++)
                res *= l;
            expr_from_longlong(res, buf, sizeof(buf));
            expr_value_set(result, buf);
        }
        expr_value_clear(&left);
        expr_value_clear(&right);
        return 1;
    }

    *result = left;
    return 1;
}

static int expr_parse_multiplicative(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_power(eval, &left))
    {
        return 0;
    }

    while (1)
    {
        expr_skip_ws(eval);
        if (eval->index < eval->length && eval->text[eval->index] == '*' &&
            !(eval->index + 1 < eval->length && eval->text[eval->index + 1] == '*'))
        {
            eval->index++;
        }
        else if (eval->index < eval->length && eval->text[eval->index] == '/')
        {
            eval->index++;
        }
        else if (eval->index < eval->length && eval->text[eval->index] == '%')
        {
            eval->index++;
        }
        else
        {
            break;
        }

        {
            ExprValue right; memset(&right, 0, sizeof(right));
            char op = eval->text[eval->index - 1];
            long long l, r;
            char buf[32];

            if (!expr_parse_power(eval, &right))
            {
                expr_value_clear(&left);
                return 0;
            }
            if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
            {
                expr_value_clear(&left);
                expr_value_clear(&right);
                return expr_error(eval, "expected numeric value");
            }
            if (op == '*')
                expr_from_longlong(l * r, buf, sizeof(buf));
            else if (op == '/')
            {
                if (r == 0)
                {
                    expr_value_clear(&left);
                    expr_value_clear(&right);
                    return expr_error(eval, "divide by zero");
                }
                expr_from_longlong(l / r, buf, sizeof(buf));
            }
            else
            {
                if (r == 0)
                {
                    expr_value_clear(&left);
                    expr_value_clear(&right);
                    return expr_error(eval, "divide by zero");
                }
                expr_from_longlong(l % r, buf, sizeof(buf));
            }
            expr_value_set(&left, buf);
            expr_value_clear(&right);
        }
    }

    *result = left;
    return 1;
}

static int expr_parse_additive(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_multiplicative(eval, &left))
    {
        return 0;
    }

    while (1)
    {
        expr_skip_ws(eval);
        if (eval->index < eval->length && (eval->text[eval->index] == '+' || eval->text[eval->index] == '-'))
        {
            char op = eval->text[eval->index++];
            ExprValue right; memset(&right, 0, sizeof(right));
            long long l, r;
            char buf[32];

            if (!expr_parse_multiplicative(eval, &right))
            {
                expr_value_clear(&left);
                return 0;
            }
            if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
            {
                expr_value_clear(&left);
                expr_value_clear(&right);
                return expr_error(eval, "expected numeric value");
            }
            if (op == '+')
                expr_from_longlong(l + r, buf, sizeof(buf));
            else
                expr_from_longlong(l - r, buf, sizeof(buf));
            expr_value_set(&left, buf);
            expr_value_clear(&right);
        }
        else
        {
            break;
        }
    }

    *result = left;
    return 1;
}

static int expr_parse_shift(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_additive(eval, &left))
    {
        return 0;
    }

    while (1)
    {
        expr_skip_ws(eval);
        if (eval->index + 1 < eval->length && eval->text[eval->index] == '<' && eval->text[eval->index + 1] == '<')
        {
            eval->index += 2;
        }
        else if (eval->index + 1 < eval->length && eval->text[eval->index] == '>' && eval->text[eval->index + 1] == '>')
        {
            eval->index += 2;
        }
        else
        {
            break;
        }

        {
            ExprValue right; memset(&right, 0, sizeof(right));
            long long l, r;
            char buf[32];

            if (!expr_parse_additive(eval, &right))
            {
                expr_value_clear(&left);
                return 0;
            }
            if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
            {
                expr_value_clear(&left);
                expr_value_clear(&right);
                return expr_error(eval, "expected numeric value");
            }
            if (eval->text[eval->index - 2] == '<')
                expr_from_longlong(l << r, buf, sizeof(buf));
            else
                expr_from_longlong(l >> r, buf, sizeof(buf));
            expr_value_set(&left, buf);
            expr_value_clear(&right);
        }
    }

    *result = left;
    return 1;
}

static int expr_parse_relational(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_shift(eval, &left))
    {
        return 0;
    }

    while (1)
    {
        int op = 0;
        expr_skip_ws(eval);
        if (eval->index + 1 < eval->length && eval->text[eval->index] == '<' && eval->text[eval->index + 1] == '=')
        {
            op = 1;
            eval->index += 2;
        }
        else if (eval->index + 1 < eval->length && eval->text[eval->index] == '>' && eval->text[eval->index + 1] == '=')
        {
            op = 2;
            eval->index += 2;
        }
        else if (eval->index < eval->length && eval->text[eval->index] == '<')
        {
            op = 3;
            eval->index++;
        }
        else if (eval->index < eval->length && eval->text[eval->index] == '>')
        {
            op = 4;
            eval->index++;
        }
        else if (expr_match_operator(eval, "in"))
        {
            op = 5;
        }
        else if (expr_match_operator(eval, "ni"))
        {
            op = 6;
        }
        else
        {
            break;
        }

        {
            ExprValue right; memset(&right, 0, sizeof(right));
            if (!expr_parse_shift(eval, &right))
            {
                expr_value_clear(&left);
                return 0;
            }

            if (op == 5 || op == 6)
            {
                int found = 0;
                const char *haystack = right.str;
                const char *needle = left.str;
                size_t needle_len = strlen(needle);
                const char *p = haystack;

                while (*p)
                {
                    while (*p && isspace((unsigned char)*p))
                        p++;
                    if (strncmp(p, needle, needle_len) == 0 && (p[needle_len] == '\0' || isspace((unsigned char)p[needle_len])))
                    {
                        found = 1;
                        break;
                    }
                    while (*p && !isspace((unsigned char)*p))
                        p++;
                }

                expr_value_set(&left, ((op == 5) == found) ? "1" : "0");
            }
            else
            {
                long long l, r;
                int cmp_result;
                if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
                {
                    expr_value_clear(&left);
                    expr_value_clear(&right);
                    return expr_error(eval, "expected numeric value");
                }
                switch (op)
                {
                    case 1: cmp_result = l <= r; break;
                    case 2: cmp_result = l >= r; break;
                    case 3: cmp_result = l < r; break;
                    case 4: cmp_result = l > r; break;
                    default: cmp_result = 0; break;
                }
                expr_value_set(&left, cmp_result ? "1" : "0");
            }
            expr_value_clear(&right);
        }
    }

    *result = left;
    return 1;
}

static int expr_parse_equality(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_relational(eval, &left))
    {
        return 0;
    }

    while (1)
    {
        int op = 0;
        expr_skip_ws(eval);
        if (eval->index + 1 < eval->length && eval->text[eval->index] == '=' && eval->text[eval->index + 1] == '=')
        {
            op = 1;
            eval->index += 2;
        }
        else if (eval->index + 1 < eval->length && eval->text[eval->index] == '!' && eval->text[eval->index + 1] == '=')
        {
            op = 2;
            eval->index += 2;
        }
        else if (expr_match_operator(eval, "eq"))
        {
            op = 3;
        }
        else if (expr_match_operator(eval, "ne"))
        {
            op = 4;
        }
        else
        {
            break;
        }

        {
            ExprValue right; memset(&right, 0, sizeof(right));
            if (!expr_parse_relational(eval, &right))
            {
                expr_value_clear(&left);
                return 0;
            }

            if (op == 3 || op == 4)
            {
                int cmp = strcmp(left.str, right.str) == 0;
                expr_value_set(&left, ((op == 3) == cmp) ? "1" : "0");
            }
            else
            {
                long long l, r;
                if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
                {
                    expr_value_clear(&left);
                    expr_value_clear(&right);
                    return expr_error(eval, "expected numeric value");
                }
                expr_value_set(&left, ((op == 1) == (l == r)) ? "1" : "0");
            }
            expr_value_clear(&right);
        }
    }

    *result = left;
    return 1;
}

static int expr_parse_bitwise_and(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_equality(eval, &left))
    {
        return 0;
    }

    while (expr_match_single_operator(eval, '&', '&'))
    {
        ExprValue right; memset(&right, 0, sizeof(right));
        long long l, r;
        char buf[32];

        if (!expr_parse_equality(eval, &right))
        {
            expr_value_clear(&left);
            return 0;
        }
        if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
        {
            expr_value_clear(&left);
            expr_value_clear(&right);
            return expr_error(eval, "expected numeric value");
        }
        expr_from_longlong(l & r, buf, sizeof(buf));
        expr_value_set(&left, buf);
        expr_value_clear(&right);
    }

    *result = left;
    return 1;
}

static int expr_parse_bitwise_xor(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_bitwise_and(eval, &left))
    {
        return 0;
    }

    while (expr_match_operator(eval, "^"))
    {
        ExprValue right; memset(&right, 0, sizeof(right));
        long long l, r;
        char buf[32];

        if (!expr_parse_bitwise_and(eval, &right))
        {
            expr_value_clear(&left);
            return 0;
        }
        if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
        {
            expr_value_clear(&left);
            expr_value_clear(&right);
            return expr_error(eval, "expected numeric value");
        }
        expr_from_longlong(l ^ r, buf, sizeof(buf));
        expr_value_set(&left, buf);
        expr_value_clear(&right);
    }

    *result = left;
    return 1;
}

static int expr_parse_bitwise_or(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_bitwise_xor(eval, &left))
    {
        return 0;
    }

    while (expr_match_single_operator(eval, '|', '|'))
    {
        ExprValue right; memset(&right, 0, sizeof(right));
        long long l, r;
        char buf[32];

        if (!expr_parse_bitwise_xor(eval, &right))
        {
            expr_value_clear(&left);
            return 0;
        }
        if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
        {
            expr_value_clear(&left);
            expr_value_clear(&right);
            return expr_error(eval, "expected numeric value");
        }
        expr_from_longlong(l | r, buf, sizeof(buf));
        expr_value_set(&left, buf);
        expr_value_clear(&right);
    }

    *result = left;
    return 1;
}

static int expr_parse_logical_and(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_bitwise_or(eval, &left))
    {
        return 0;
    }

    while (expr_match_operator(eval, "&&"))
    {
        ExprValue right; memset(&right, 0, sizeof(right));
        long long l, r;
        char buf[32];

        if (!expr_parse_bitwise_or(eval, &right))
        {
            expr_value_clear(&left);
            return 0;
        }
        if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
        {
            expr_value_clear(&left);
            expr_value_clear(&right);
            return expr_error(eval, "expected numeric value");
        }
        expr_from_longlong(l && r, buf, sizeof(buf));
        expr_value_set(&left, buf);
        expr_value_clear(&right);
    }

    *result = left;
    return 1;
}

static int expr_parse_logical_or(ExprEval *eval, ExprValue *result)
{
    ExprValue left; memset(&left, 0, sizeof(left));
    if (!expr_parse_logical_and(eval, &left))
    {
        return 0;
    }

    while (expr_match_operator(eval, "||"))
    {
        ExprValue right; memset(&right, 0, sizeof(right));
        long long l, r;
        char buf[32];

        if (!expr_parse_logical_and(eval, &right))
        {
            expr_value_clear(&left);
            return 0;
        }
        if (!expr_to_longlong(left.str, &l) || !expr_to_longlong(right.str, &r))
        {
            expr_value_clear(&left);
            expr_value_clear(&right);
            return expr_error(eval, "expected numeric value");
        }
        expr_from_longlong(l || r, buf, sizeof(buf));
        expr_value_set(&left, buf);
        expr_value_clear(&right);
    }

    *result = left;
    return 1;
}

static int expr_parse_ternary(ExprEval *eval, ExprValue *result)
{
    ExprValue condition; memset(&condition, 0, sizeof(condition));
    if (!expr_parse_logical_or(eval, &condition))
    {
        return 0;
    }

    if (expr_match_char(eval, '?'))
    {
        ExprValue true_val; memset(&true_val, 0, sizeof(true_val));
        ExprValue false_val; memset(&false_val, 0, sizeof(false_val));
        long long cond_val;

        if (!expr_to_longlong(condition.str, &cond_val))
        {
            expr_value_clear(&condition);
            return expr_error(eval, "expected numeric value");
        }
        expr_value_clear(&condition);

        if (!expr_parse_expression(eval, &true_val))
            return 0;

        if (!expr_match_char(eval, ':'))
        {
            expr_value_clear(&true_val);
            return expr_error(eval, "expected ':' in ternary expression");
        }

        if (!expr_parse_expression(eval, &false_val))
        {
            expr_value_clear(&true_val);
            return 0;
        }

        if (cond_val)
        {
            *result = true_val;
            expr_value_clear(&false_val);
        }
        else
        {
            *result = false_val;
            expr_value_clear(&true_val);
        }
        return 1;
    }

    *result = condition;
    return 1;
}

static int expr_parse_expression(ExprEval *eval, ExprValue *result)
{
    return expr_parse_ternary(eval, result);
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
    ExprValue value;

    eval.text = text;
    eval.length = strlen(text);
    eval.index = 0;
    eval.line = line;
    eval.column = column;
    eval.context = context;
    eval.error = error;

    memset(&value, 0, sizeof(value));


    if (!expr_parse_expression(&eval, &value))
    {
        return 0;
    }

    expr_skip_ws(&eval);
    if (eval.index != eval.length)
    {
        expr_value_clear(&value);
        tcl_error_set(error, TCL_ERROR_SYNTAX, line, column, "unexpected token in expression");
        return 0;
    }

    if (!value.str)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
        return 0;
    }

    *result = (char *)malloc(strlen(value.str) + 1);
    if (!*result)
    {
        expr_value_clear(&value);
        tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
        return 0;
    }

    strcpy(*result, value.str);
    expr_value_clear(&value);
    return 1;
}
