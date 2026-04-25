#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/executor.h"
#include "runtime/executor/internal.h"
#include "runtime/variables.h"
#include "runtime/validator.h"
#include "core/script_parser.h"
#include "core/errors.h"

/* -------------------------------------------------------------------------- */
/*  Text buffer utilities                                                     */
/* -------------------------------------------------------------------------- */

int append_text(char **buffer, size_t *capacity, size_t *length, const char *text, size_t text_length)
{
    if (*length + text_length + 1 > *capacity)
    {
        size_t new_capacity = *capacity;

        while (*length + text_length + 1 > new_capacity)
        {
            new_capacity *= 2;
        }

        char *resized = (char *)realloc(*buffer, new_capacity);
        if (!resized)
        {
            return 0;
        }

        *buffer = resized;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    (*buffer)[*length] = '\0';
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  Variable name scanning                                                    */
/* -------------------------------------------------------------------------- */

static int is_variable_name_char(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') ||
           character == '_';
}

size_t scan_variable_name(const char *text, size_t length, size_t start)
{
    size_t index = start;

    while (index < length)
    {
        if (is_variable_name_char(text[index]))
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

/* -------------------------------------------------------------------------- */
/*  Backslash / escape decoding                                               */
/* -------------------------------------------------------------------------- */

static int hex_digit_value(char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }

    if (character >= 'a' && character <= 'f')
    {
        return character - 'a' + 10;
    }

    if (character >= 'A' && character <= 'F')
    {
        return character - 'A' + 10;
    }

    return -1;
}

static size_t encode_utf8(unsigned int codepoint, char output[4])
{
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
    {
        codepoint = 0xFFFD;
    }

    if (codepoint <= 0x7F)
    {
        output[0] = (char)codepoint;
        return 1;
    }

    if (codepoint <= 0x7FF)
    {
        output[0] = (char)(0xC0 | (codepoint >> 6));
        output[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }

    if (codepoint <= 0xFFFF)
    {
        output[0] = (char)(0xE0 | (codepoint >> 12));
        output[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        output[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }

    output[0] = (char)(0xF0 | (codepoint >> 18));
    output[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    output[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    output[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

static int decode_octal_escape(
    const char *text,
    size_t length,
    size_t start,
    unsigned int *codepoint,
    size_t *digits_consumed)
{
    unsigned int value = 0;
    size_t index = start;
    size_t count = 0;

    while (index < length && count < 3 && text[index] >= '0' && text[index] <= '7')
    {
        unsigned int digit = (unsigned int)(text[index] - '0');
        if (value > ((0xFFU - digit) >> 3))
        {
            break;
        }
        value = (value << 3) + digit;
        index++;
        count++;
    }

    if (count == 0)
    {
        return 0;
    }

    *codepoint = value;
    *digits_consumed = count;
    return 1;
}

static int decode_hex_escape(
    const char *text,
    size_t length,
    size_t start,
    size_t max_digits,
    unsigned int max_value,
    unsigned int *codepoint,
    size_t *digits_consumed)
{
    unsigned int value = 0;
    size_t index = start;
    size_t count = 0;

    while (index < length && count < max_digits)
    {
        int digit = hex_digit_value(text[index]);
        if (digit < 0)
        {
            break;
        }

        if (value > ((max_value - (unsigned int)digit) >> 4))
        {
            break;
        }

        value = (value << 4) + (unsigned int)digit;
        index++;
        count++;
    }

    if (count == 0)
    {
        return 0;
    }

    *codepoint = value;
    *digits_consumed = count;
    return 1;
}

static int decode_backslash_sequence(
    const char *text,
    size_t length,
    size_t index,
    char output[4],
    size_t *output_length,
    size_t *consumed_length)
{
    unsigned int codepoint = 0;
    size_t digits = 0;
    char next;

    if (index >= length || text[index] != '\\')
    {
        return 0;
    }

    if (index + 1 >= length)
    {
        output[0] = '\\';
        *output_length = 1;
        *consumed_length = 1;
        return 1;
    }

    next = text[index + 1];
    if (next == '\n' || next == '\r')
    {
        size_t cursor = index + 2;
        if (next == '\r' && cursor < length && text[cursor] == '\n')
        {
            cursor++;
        }

        while (cursor < length && (text[cursor] == ' ' || text[cursor] == '\t'))
        {
            cursor++;
        }

        output[0] = ' ';
        *output_length = 1;
        *consumed_length = cursor - index;
        return 1;
    }

    switch (next)
    {
        case 'a':
            codepoint = '\a';
            break;
        case 'b':
            codepoint = '\b';
            break;
        case 'f':
            codepoint = '\f';
            break;
        case 'n':
            codepoint = '\n';
            break;
        case 'r':
            codepoint = '\r';
            break;
        case 't':
            codepoint = '\t';
            break;
        case 'v':
            codepoint = '\v';
            break;
        case '\\':
            codepoint = '\\';
            break;
        case 'x':
            if (decode_hex_escape(text, length, index + 2, 2, 0xFFU, &codepoint, &digits))
            {
                *output_length = encode_utf8(codepoint, output);
                *consumed_length = 2 + digits;
                return 1;
            }
            output[0] = 'x';
            *output_length = 1;
            *consumed_length = 2;
            return 1;
        case 'u':
            if (decode_hex_escape(text, length, index + 2, 4, 0xFFFFU, &codepoint, &digits))
            {
                *output_length = encode_utf8(codepoint, output);
                *consumed_length = 2 + digits;
                return 1;
            }
            output[0] = 'u';
            *output_length = 1;
            *consumed_length = 2;
            return 1;
        case 'U':
            if (decode_hex_escape(text, length, index + 2, 8, 0x10FFFFU, &codepoint, &digits))
            {
                *output_length = encode_utf8(codepoint, output);
                *consumed_length = 2 + digits;
                return 1;
            }
            output[0] = 'U';
            *output_length = 1;
            *consumed_length = 2;
            return 1;
        default:
            if (decode_octal_escape(text, length, index + 1, &codepoint, &digits))
            {
                *output_length = encode_utf8(codepoint, output);
                *consumed_length = 1 + digits;
                return 1;
            }

            output[0] = next;
            *output_length = 1;
            *consumed_length = 2;
            return 1;
    }

    *output_length = encode_utf8(codepoint, output);
    *consumed_length = 2;
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  Command substitution                                                      */
/* -------------------------------------------------------------------------- */

int execute_command_substitution(
    ExecutorContext *context,
    const char *text,
    size_t length,
    size_t start,
    size_t *end,
    TclError *error,
    char **result)
{
    size_t inner_length = *end - start - 1;
    char *inner_text;
    AstCommand *sub_program;

    (void)length;

    inner_text = (char *)malloc(inner_length + 1);
    if (!inner_text)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, 0, 0, "out of memory");
        return 0;
    }

    memcpy(inner_text, text + start + 1, inner_length);
    inner_text[inner_length] = '\0';

    sub_program = parse_script(inner_text, 1, 1, error);
    if (!sub_program)
    {
        free(inner_text);
        return 0;
    }

    if (!executor_execute(context, sub_program, error))
    {
        free(inner_text);
        ast_command_free(sub_program);
        return 0;
    }

    free(inner_text);
    ast_command_free(sub_program);

    *result = context->result ? (char *)malloc(strlen(context->result) + 1) : (char *)malloc(1);
    if (!*result)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, 0, 0, "out of memory");
        return 0;
    }

    if (context->result)
    {
        strcpy(*result, context->result);
    }
    else
    {
        (*result)[0] = '\0';
    }

    return 1;
}

int execute_script_text(
    ExecutorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error)
{
    AstCommand *program;
    size_t i;
    int empty = 1;

    for (i = 0; text[i]; i++)
    {
        if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r')
        {
            empty = 0;
            break;
        }
    }

    if (empty)
    {
        return 1;
    }

    program = parse_script(text, line, column, error);
    if (!program)
    {
        return 0;
    }

    if (!executor_execute(context, program, error))
    {
        ast_command_free(program);
        return 0;
    }

    ast_command_free(program);
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  Word type helpers                                                         */
/* -------------------------------------------------------------------------- */

static int is_expand_word_type(AstWordType type)
{
    return type == AST_WORD_EXPAND_EMPTY ||
           type == AST_WORD_EXPAND_STRING ||
           type == AST_WORD_EXPAND_QUOTED ||
           type == AST_WORD_EXPAND_BRACED ||
           type == AST_WORD_EXPAND_VAR ||
           type == AST_WORD_EXPAND_VAR_BRACED;
}

static AstWordType expansion_base_type(AstWordType type)
{
    switch (type)
    {
        case AST_WORD_EXPAND_STRING:
            return AST_WORD_STRING;
        case AST_WORD_EXPAND_QUOTED:
            return AST_WORD_QUOTED;
        case AST_WORD_EXPAND_BRACED:
            return AST_WORD_BRACED;
        case AST_WORD_EXPAND_VAR:
            return AST_WORD_VAR;
        case AST_WORD_EXPAND_VAR_BRACED:
            return AST_WORD_VAR_BRACED;
        case AST_WORD_EXPAND_EMPTY:
        default:
            return AST_WORD_STRING;
    }
}

/* -------------------------------------------------------------------------- */
/*  resolve_string_text – $var, \escapes, [cmd]                               */
/* -------------------------------------------------------------------------- */

int resolve_string_text(
    ExecutorContext *context,
    const char *source_text,
    const SourceSpan *span,
    TclError *error,
    char **result)
{
    size_t source_index;
    size_t source_length;
    size_t capacity;
    size_t result_length;
    char *buffer;

    source_index = 0;
    source_length = strlen(source_text);
    capacity = source_length + 1;
    result_length = 0;

    buffer = (char *)malloc(capacity);
    if (!buffer)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
        return 0;
    }

    buffer[0] = '\0';

    while (source_index < source_length)
    {
        if (source_text[source_index] == '\\')
        {
            char replacement[4];
            size_t replacement_length = 0;
            size_t consumed_length = 0;

            if (!decode_backslash_sequence(
                    source_text,
                    source_length,
                    source_index,
                    replacement,
                    &replacement_length,
                    &consumed_length))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "failed to decode backslash sequence");
                return 0;
            }

            if (!append_text(&buffer, &capacity, &result_length, replacement, replacement_length))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                return 0;
            }
            source_index += consumed_length;
            continue;
        }

        if (source_text[source_index] == '[')
        {
            size_t bracket_end;
            char *script_result = NULL;
            if (!validator_find_matching_bracket(source_text, source_index, &bracket_end))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYNTAX, span->line, span->column, "unterminated command substitution");
                return 0;
            }
            if (!execute_command_substitution(context, source_text, source_length, source_index, &bracket_end, error, &script_result))
            {
                free(buffer);
                return 0;
            }
            if (!append_text(&buffer, &capacity, &result_length, script_result, strlen(script_result)))
            {
                free(script_result);
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                return 0;
            }
            free(script_result);
            source_index = bracket_end + 1;
            continue;
        }

        if (source_text[source_index] != '$')
        {
            if (!append_text(&buffer, &capacity, &result_length, source_text + source_index, 1))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                return 0;
            }
            source_index++;
            continue;
        }

        source_index++;
        if (source_index >= source_length)
        {
            if (!append_text(&buffer, &capacity, &result_length, "$", 1))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                return 0;
            }
            break;
        }

        size_t name_start;
        size_t name_length;

        if (source_text[source_index] == '{')
        {
            source_index++;
            name_start = source_index;

            while (source_index < source_length && source_text[source_index] != '}')
            {
                source_index++;
            }

            if (source_index >= source_length)
            {
                free(buffer);
                tcl_error_setf(
                    error,
                    TCL_ERROR_SYNTAX,
                    span->line,
                    span->column,
                    "unterminated braced variable reference");
                return 0;
            }

            name_length = source_index - name_start;
            source_index++;
        }
        else
        {
            name_start = source_index;
            name_length = scan_variable_name(source_text, source_length, source_index);
            source_index += name_length;

            if (source_index < source_length && source_text[source_index] == '(')
            {
                size_t base_name_length = name_length;
                size_t index_start;
                size_t index_length;
                size_t variable_name_length;
                int depth = 1;
                char *index_text = NULL;
                char *resolved_index = NULL;
                char *array_name = NULL;

                source_index++;
                index_start = source_index;
                while (source_index < source_length)
                {
                    if (source_text[source_index] == '\\' && source_index + 1 < source_length)
                    {
                        source_index += 2;
                        continue;
                    }

                    if (source_text[source_index] == '(')
                    {
                        depth++;
                    }
                    else if (source_text[source_index] == ')')
                    {
                        depth--;
                        if (depth == 0)
                        {
                            break;
                        }
                    }
                    source_index++;
                }

                if (depth != 0)
                {
                    free(buffer);
                    tcl_error_set(
                        error,
                        TCL_ERROR_SYNTAX,
                        span->line,
                        span->column,
                        "unterminated array variable reference");
                    return 0;
                }

                index_length = source_index - index_start;
                source_index++;

                index_text = (char *)malloc(index_length + 1);
                if (!index_text)
                {
                    free(buffer);
                    tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                    return 0;
                }

                memcpy(index_text, source_text + index_start, index_length);
                index_text[index_length] = '\0';

                if (!resolve_string_text(context, index_text, span, error, &resolved_index))
                {
                    free(index_text);
                    free(buffer);
                    return 0;
                }

                variable_name_length = base_name_length + strlen(resolved_index) + 2;
                array_name = (char *)malloc(variable_name_length + 1);
                if (!array_name)
                {
                    free(index_text);
                    free(resolved_index);
                    free(buffer);
                    tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                    return 0;
                }

                memcpy(array_name, source_text + name_start, base_name_length);
                array_name[base_name_length] = '(';
                memcpy(array_name + base_name_length + 1, resolved_index, strlen(resolved_index));
                array_name[variable_name_length - 1] = ')';
                array_name[variable_name_length] = '\0';

                name_length = variable_name_length;

                free(index_text);
                free(resolved_index);

                {
                    const char *value = variables_get(context->variables, array_name);
                    if (!value)
                    {
                        free(buffer);
                        tcl_error_setf(
                            error,
                            TCL_ERROR_SEMANTIC,
                            span->line,
                            span->column,
                            "variable '$%s' not found",
                            array_name);
                        free(array_name);
                        return 0;
                    }

                    if (!append_text(&buffer, &capacity, &result_length, value, strlen(value)))
                    {
                        free(array_name);
                        free(buffer);
                        tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                        return 0;
                    }
                    free(array_name);
                }

                continue;
            }
        }

        if (name_length == 0)
        {
            if (!append_text(&buffer, &capacity, &result_length, "$", 1))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                return 0;
            }
            continue;
        }

        char *name = (char *)malloc(name_length + 1);
        if (!name)
        {
            free(buffer);
            tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
            return 0;
        }

        memcpy(name, source_text + name_start, name_length);
        name[name_length] = '\0';

        const char *value = variables_get(context->variables, name);

        if (!value)
        {
            free(buffer);
            tcl_error_setf(
                error,
                TCL_ERROR_SEMANTIC,
                span->line,
                span->column,
                "variable '$%s' not found",
                name);
            free(name);
            return 0;
        }

        if (!append_text(&buffer, &capacity, &result_length, value, strlen(value)))
        {
            free(name);
            free(buffer);
            tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
            return 0;
        }

        free(name);
    }

    *result = buffer;
    return 1;
}

static int resolve_string_word(
    ExecutorContext *context,
    const AstWord *word,
    TclError *error,
    char **result)
{
    return resolve_string_text(context, word->text, &word->span, error, result);
}

/* -------------------------------------------------------------------------- */
/*  evaluate_word                                                             */
/* -------------------------------------------------------------------------- */

int evaluate_word(ExecutorContext *context, const AstWord *word, TclError *error, char **result)
{
    if (word->type == AST_WORD_BRACED)
    {
        *result = (char *)malloc(strlen(word->text) + 1);
        if (!*result)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        strcpy(*result, word->text);
        return 1;
    }

    if (word->type == AST_WORD_VAR)
    {
        size_t length = strlen(word->text);
        char *prefixed = (char *)malloc(length + 2);
        int status;

        if (!prefixed)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        prefixed[0] = '$';
        memcpy(prefixed + 1, word->text, length);
        prefixed[length + 1] = '\0';

        status = resolve_string_text(context, prefixed, &word->span, error, result);
        free(prefixed);
        return status;
    }

    if (word->type == AST_WORD_VAR_BRACED)
    {
        const char *value = variables_get(context->variables, word->text);
        if (!value)
        {
            tcl_error_setf(
                error,
                TCL_ERROR_SEMANTIC,
                word->span.line,
                word->span.column,
                "variable '$%s' not found",
                word->text);
            return 0;
        }

        *result = (char *)malloc(strlen(value) + 1);
        if (!*result)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        strcpy(*result, value);
        return 1;
    }

    return resolve_string_word(context, word, error, result);
}

/* -------------------------------------------------------------------------- */
/*  List parsing (for {*})                                                    */
/* -------------------------------------------------------------------------- */

static int parse_list_braced_item(const char *text, size_t *index, size_t length, char **item)
{
    size_t start;
    int depth = 1;

    (*index)++;
    start = *index;
    while (*index < length)
    {
        if (text[*index] == '\\' && *index + 1 < length)
        {
            *index += 2;
            continue;
        }

        if (text[*index] == '{')
        {
            depth++;
        }
        else if (text[*index] == '}')
        {
            depth--;
            if (depth == 0)
            {
                size_t item_length = *index - start;
                char *copy = (char *)malloc(item_length + 1);
                if (!copy)
                {
                    return 0;
                }
                memcpy(copy, text + start, item_length);
                copy[item_length] = '\0';
                (*index)++;
                *item = copy;
                return 1;
            }
        }
        (*index)++;
    }

    return 0;
}

static int parse_list_quoted_item(const char *text, size_t *index, size_t length, char **item)
{
    size_t write_index = 0;
    char *buffer;

    (*index)++;
    buffer = (char *)malloc(length - *index + 1);
    if (!buffer)
    {
        return 0;
    }

    while (*index < length)
    {
        if (text[*index] == '"')
        {
            (*index)++;
            buffer[write_index] = '\0';
            *item = buffer;
            return 1;
        }

        if (text[*index] == '\\')
        {
            char replacement[4];
            size_t replacement_length = 0;
            size_t consumed_length = 0;
            size_t copy_index;

            if (!decode_backslash_sequence(text, length, *index, replacement, &replacement_length, &consumed_length))
            {
                free(buffer);
                return 0;
            }

            for (copy_index = 0; copy_index < replacement_length; copy_index++)
            {
                buffer[write_index++] = replacement[copy_index];
            }
            *index += consumed_length;
            continue;
        }

        buffer[write_index++] = text[*index];
        (*index)++;
    }

    free(buffer);
    return 0;
}

static int parse_list_unquoted_item(const char *text, size_t *index, size_t length, char **item)
{
    size_t write_index = 0;
    char *buffer = (char *)malloc(length - *index + 1);
    if (!buffer)
    {
        return 0;
    }

    while (*index < length && !isspace((unsigned char)text[*index]))
    {
        if (text[*index] == '\\')
        {
            char replacement[4];
            size_t replacement_length = 0;
            size_t consumed_length = 0;
            size_t copy_index;

            if (!decode_backslash_sequence(text, length, *index, replacement, &replacement_length, &consumed_length))
            {
                free(buffer);
                return 0;
            }

            for (copy_index = 0; copy_index < replacement_length; copy_index++)
            {
                buffer[write_index++] = replacement[copy_index];
            }
            *index += consumed_length;
            continue;
        }

        buffer[write_index++] = text[*index];
        (*index)++;
    }

    buffer[write_index] = '\0';
    *item = buffer;
    return 1;
}

int parse_list_item(const char *text, size_t *index, char **item)
{
    size_t length = strlen(text);

    while (*index < length && isspace((unsigned char)text[*index]))
    {
        (*index)++;
    }

    if (*index >= length)
    {
        *item = NULL;
        return 1;
    }

    if (text[*index] == '{')
    {
        return parse_list_braced_item(text, index, length, item);
    }

    if (text[*index] == '"')
    {
        return parse_list_quoted_item(text, index, length, item);
    }

    return parse_list_unquoted_item(text, index, length, item);
}

static int append_evaluated_value(char ***values, size_t *count, size_t *capacity, char *value)
{
    if (*count + 1 > *capacity)
    {
        size_t new_capacity = *capacity == 0 ? 4 : *capacity;
        char **resized;

        while (*count + 1 > new_capacity)
        {
            new_capacity *= 2;
        }

        resized = (char **)realloc(*values, sizeof(char *) * new_capacity);
        if (!resized)
        {
            return 0;
        }

        *values = resized;
        *capacity = new_capacity;
    }

    (*values)[(*count)++] = value;
    return 1;
}

static int split_list_for_expansion(
    const AstWord *word,
    const char *list_text,
    TclError *error,
    char ***items,
    size_t *item_count)
{
    size_t index = 0;
    size_t capacity = 0;
    size_t count = 0;
    char **values = NULL;

    while (1)
    {
        char *item = NULL;
        if (!parse_list_item(list_text, &index, &item))
        {
            free_values(values, count);
            tcl_error_set(error, TCL_ERROR_SYNTAX, word->span.line, word->span.column, "malformed list for argument expansion");
            return 0;
        }

        if (!item)
        {
            break;
        }

        if (!append_evaluated_value(&values, &count, &capacity, item))
        {
            free(item);
            free_values(values, count);
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }
    }

    *items = values;
    *item_count = count;
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  evaluate_expansion_word                                                   */
/* -------------------------------------------------------------------------- */

int evaluate_expansion_word(
    ExecutorContext *context,
    const AstWord *word,
    TclError *error,
    char ***items,
    size_t *item_count)
{
    char *value = NULL;
    AstWord base_word;

    if (word->type == AST_WORD_EXPAND_EMPTY)
    {
        *items = NULL;
        *item_count = 0;
        return 1;
    }

    base_word = *word;
    base_word.type = expansion_base_type(word->type);

    if (!evaluate_word(context, &base_word, error, &value))
    {
        return 0;
    }

    if (!split_list_for_expansion(word, value, error, items, item_count))
    {
        free(value);
        return 0;
    }

    free(value);
    return 1;
}

/* -------------------------------------------------------------------------- */
/*  evaluate_words                                                            */
/* -------------------------------------------------------------------------- */

int evaluate_words(
    ExecutorContext *context,
    const AstWord *head,
    size_t count,
    TclError *error,
    char ***values,
    size_t *value_count)
{
    const AstWord *word;
    char **evaluated = NULL;
    size_t evaluated_count = 0;
    size_t evaluated_capacity;
    size_t index;

    evaluated_capacity = count == 0 ? 1 : count;
    evaluated = (char **)calloc(evaluated_capacity, sizeof(char *));
    if (!evaluated)
    {
        return 0;
    }

    word = head;
    for (index = 0; index < count; index++)
    {
        if (is_expand_word_type(word->type))
        {
            char **expanded = NULL;
            size_t expanded_count = 0;
            size_t expanded_index;

            if (!evaluate_expansion_word(context, word, error, &expanded, &expanded_count))
            {
                free_values(evaluated, evaluated_count);
                return 0;
            }

            for (expanded_index = 0; expanded_index < expanded_count; expanded_index++)
            {
                if (!append_evaluated_value(
                        &evaluated,
                        &evaluated_count,
                        &evaluated_capacity,
                        expanded[expanded_index]))
                {
                    free_values(expanded, expanded_count);
                    free_values(evaluated, evaluated_count);
                    tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
                    return 0;
                }
            }
            free(expanded);
        }
        else
        {
            char *single = NULL;
            if (!evaluate_word(context, word, error, &single))
            {
                free_values(evaluated, evaluated_count);
                return 0;
            }

            if (!append_evaluated_value(&evaluated, &evaluated_count, &evaluated_capacity, single))
            {
                free(single);
                free_values(evaluated, evaluated_count);
                tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
                return 0;
            }
        }

        word = word->next;
    }

    *values = evaluated;
    *value_count = evaluated_count;
    return 1;
}
