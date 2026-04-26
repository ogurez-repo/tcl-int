#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "core/errors.h"
#include "core/script_parser.h"
#include "runtime/executor.h"
#include "runtime/executor/internal.h"
#include "runtime/validator.h"
#include "runtime/variables.h"


int append_text(char **buffer, size_t *capacity, size_t *length, const char *text, size_t text_length)
{
    if (*length + text_length + 1 > *capacity)
    {
        size_t new_capacity = *capacity;

        while (*length + text_length + 1 > new_capacity)
        {
            new_capacity *= 2;
        }

        {
            char *resized = (char *)realloc(*buffer, new_capacity);
            if (!resized)
            {
                return 0;
            }

            *buffer = resized;
            *capacity = new_capacity;
        }
    }

    memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    (*buffer)[*length] = '\0';
    return 1;
}


static int is_var_start_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_var_char(char c)
{
    return is_var_start_char(c) || (c >= '0' && c <= '9');
}

size_t scan_variable_name(const char *text, size_t length, size_t start)
{
    size_t index = start;

    if (index >= length || !is_var_start_char(text[index]))
    {
        return 0;
    }

    index++;
    while (index < length && is_var_char(text[index]))
    {
        index++;
    }

    return index - start;
}

static int is_supported_var_name(const char *name)
{
    size_t index;

    if (!name || name[0] == '\0' || !is_var_start_char(name[0]))
    {
        return 0;
    }

    for (index = 1; name[index]; index++)
    {
        if (!is_var_char(name[index]))
        {
            return 0;
        }
    }

    return 1;
}


static int decode_escape(
    const char *text,
    size_t length,
    size_t index,
    char *out_char,
    size_t *consumed)
{
    char next;

    if (index >= length || text[index] != '\\')
    {
        return 0;
    }

    if (index + 1 >= length)
    {
        *out_char = '\\';
        *consumed = 1;
        return 1;
    }

    next = text[index + 1];
    switch (next)
    {
        case 'n':
            *out_char = '\n';
            *consumed = 2;
            return 1;
        case 't':
            *out_char = '\t';
            *consumed = 2;
            return 1;
        case '\\':
            *out_char = '\\';
            *consumed = 2;
            return 1;
        case '"':
            *out_char = '"';
            *consumed = 2;
            return 1;
        case '{':
            *out_char = '{';
            *consumed = 2;
            return 1;
        case '}':
            *out_char = '}';
            *consumed = 2;
            return 1;
        case '[':
            *out_char = '[';
            *consumed = 2;
            return 1;
        case ']':
            *out_char = ']';
            *consumed = 2;
            return 1;
        case '$':
            *out_char = '$';
            *consumed = 2;
            return 1;
        case ';':
            *out_char = ';';
            *consumed = 2;
            return 1;
        default:
            /* Tcl-style fallback: unknown escape removes backslash. */
            *out_char = next;
            *consumed = 2;
            return 1;
    }
}


ExecCode execute_script_text_with_code(
    ExecutorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error,
    char **result)
{
    AstCommand *program;
    size_t index;
    int only_whitespace = 1;

    for (index = 0; text[index]; index++)
    {
        if (text[index] != ' ' && text[index] != '\t' && text[index] != '\n' && text[index] != '\r')
        {
            only_whitespace = 0;
            break;
        }
    }

    if (only_whitespace)
    {
        *result = (char *)malloc(1);
        if (!*result)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
            return EXEC_CODE_ERROR;
        }

        (*result)[0] = '\0';
        return EXEC_CODE_OK;
    }

    program = parse_script(text, line, column, error);
    if (!program)
    {
        return EXEC_CODE_ERROR;
    }

    {
        ExecCode code = executor_execute_program(context, program, error, result);
        ast_command_free(program);
        return code;
    }
}

int execute_script_text(
    ExecutorContext *context,
    const char *text,
    int line,
    int column,
    TclError *error)
{
    char *script_result = NULL;
    ExecCode code = execute_script_text_with_code(context, text, line, column, error, &script_result);

    if (code != EXEC_CODE_OK)
    {
        free(script_result);

        if (error->type == TCL_ERROR_NONE)
        {
            if (code == EXEC_CODE_BREAK)
            {
                tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "break outside loop");
            }
            else if (code == EXEC_CODE_CONTINUE)
            {
                tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "continue outside loop");
            }
            else if (code == EXEC_CODE_RETURN)
            {
                tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "return outside procedure");
            }
        }

        return 0;
    }

    free(context->result);
    context->result = script_result;
    return 1;
}

int execute_command_substitution(
    ExecutorContext *context,
    const char *text,
    size_t length,
    size_t start,
    int base_line,
    int base_column,
    size_t *end,
    TclError *error,
    char **result)
{
    size_t inner_length;
    char *inner_text;
    char *inner_result = NULL;
    ExecCode code;
    int line = base_line;
    int column = base_column;
    size_t index;

    (void)length;

    for (index = 0; index < start; index++)
    {
        if (text[index] == '\n')
        {
            line++;
            column = 1;
        }
        else
        {
            column++;
        }
    }

    inner_length = *end - start - 1;
    inner_text = (char *)malloc(inner_length + 1);
    if (!inner_text)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
        return 0;
    }

    memcpy(inner_text, text + start + 1, inner_length);
    inner_text[inner_length] = '\0';

    code = execute_script_text_with_code(context, inner_text, line, column + 1, error, &inner_result);
    free(inner_text);

    if (code != EXEC_CODE_OK)
    {
        free(inner_result);

        if (error->type == TCL_ERROR_NONE)
        {
            if (code == EXEC_CODE_BREAK)
            {
                tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "break outside loop");
            }
            else if (code == EXEC_CODE_CONTINUE)
            {
                tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "continue outside loop");
            }
            else if (code == EXEC_CODE_RETURN)
            {
                tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "return outside procedure");
            }
            else
            {
                tcl_error_set(error, TCL_ERROR_SEMANTIC, line, column, "command substitution failed");
            }
        }

        return 0;
    }

    if (!inner_result)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, line, column, "out of memory");
        return 0;
    }

    *result = inner_result;
    return 1;
}

static int append_variable_value(
    ExecutorContext *context,
    const char *name,
    const SourceSpan *span,
    char **buffer,
    size_t *capacity,
    size_t *result_length,
    TclError *error)
{
    const char *value;

    if (!is_supported_var_name(name))
    {
        tcl_error_set(error, TCL_ERROR_SEMANTIC, span->line, span->column, "array/namespace variables are not supported");
        return 0;
    }

    value = variables_get(context->variables, name);
    if (!value)
    {
        tcl_error_setf(error, TCL_ERROR_SEMANTIC, span->line, span->column, "Undefined variable: %s", name);
        return 0;
    }

    if (!append_text(buffer, capacity, result_length, value, strlen(value)))
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
        return 0;
    }

    return 1;
}

int resolve_string_text(
    ExecutorContext *context,
    const char *source_text,
    const SourceSpan *span,
    TclError *error,
    char **result)
{
    size_t source_index = 0;
    size_t source_length = strlen(source_text);
    size_t capacity = source_length + 1;
    size_t result_length = 0;
    char *buffer = (char *)malloc(capacity);

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
            char decoded = '\0';
            size_t consumed = 0;

            if (!decode_escape(source_text, source_length, source_index, &decoded, &consumed))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "failed to decode escape");
                return 0;
            }

            if (!append_text(&buffer, &capacity, &result_length, &decoded, 1))
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                return 0;
            }

            source_index += consumed;
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

            if (!execute_command_substitution(
                    context,
                    source_text,
                    source_length,
                    source_index,
                    span->line,
                    span->column,
                    &bracket_end,
                    error,
                    &script_result))
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

        if (source_text[source_index] == '$')
        {
            size_t name_start;
            size_t name_length;
            char *name;
            int ok;

            source_index++;
            if (source_index >= source_length)
            {
                if (!append_text(&buffer, &capacity, &result_length, "$", 1))
                {
                    free(buffer);
                    tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                    return 0;
                }
                continue;
            }

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
                    tcl_error_set(error, TCL_ERROR_SYNTAX, span->line, span->column, "unterminated braced variable reference");
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

                if (name_length > 0 && source_index < source_length && source_text[source_index] == '(')
                {
                    free(buffer);
                    tcl_error_set(error, TCL_ERROR_SEMANTIC, span->line, span->column, "array/namespace variables are not supported");
                    return 0;
                }

                if (name_length > 0 &&
                    source_index + 1 < source_length &&
                    source_text[source_index] == ':' &&
                    source_text[source_index + 1] == ':')
                {
                    free(buffer);
                    tcl_error_set(error, TCL_ERROR_SEMANTIC, span->line, span->column, "array/namespace variables are not supported");
                    return 0;
                }
            }

            if (name_length == 0)
            {
                if (source_text[source_index] == ':')
                {
                    free(buffer);
                    tcl_error_set(error, TCL_ERROR_SEMANTIC, span->line, span->column, "array/namespace variables are not supported");
                    return 0;
                }

                if (!append_text(&buffer, &capacity, &result_length, "$", 1))
                {
                    free(buffer);
                    tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                    return 0;
                }
                continue;
            }

            name = (char *)malloc(name_length + 1);
            if (!name)
            {
                free(buffer);
                tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
                return 0;
            }

            memcpy(name, source_text + name_start, name_length);
            name[name_length] = '\0';

            ok = append_variable_value(context, name, span, &buffer, &capacity, &result_length, error);
            free(name);
            if (!ok)
            {
                free(buffer);
                return 0;
            }

            continue;
        }

        if (!append_text(&buffer, &capacity, &result_length, source_text + source_index, 1))
        {
            free(buffer);
            tcl_error_set(error, TCL_ERROR_SYSTEM, span->line, span->column, "out of memory");
            return 0;
        }

        source_index++;
    }

    *result = buffer;
    return 1;
}

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

    if (word->type == AST_WORD_VAR || word->type == AST_WORD_VAR_BRACED)
    {
        size_t length = strlen(word->text);
        char *prefixed;
        int status;

        prefixed = (char *)malloc(length + 4);
        if (!prefixed)
        {
            tcl_error_set(error, TCL_ERROR_SYSTEM, word->span.line, word->span.column, "out of memory");
            return 0;
        }

        if (word->type == AST_WORD_VAR_BRACED)
        {
            prefixed[0] = '$';
            prefixed[1] = '{';
            memcpy(prefixed + 2, word->text, length);
            prefixed[length + 2] = '}';
            prefixed[length + 3] = '\0';
        }
        else
        {
            prefixed[0] = '$';
            memcpy(prefixed + 1, word->text, length);
            prefixed[length + 1] = '\0';
        }

        status = resolve_string_text(context, prefixed, &word->span, error, result);
        free(prefixed);
        return status;
    }

    if (word->type == AST_WORD_EXPAND_EMPTY ||
        word->type == AST_WORD_EXPAND_STRING ||
        word->type == AST_WORD_EXPAND_QUOTED ||
        word->type == AST_WORD_EXPAND_BRACED ||
        word->type == AST_WORD_EXPAND_VAR ||
        word->type == AST_WORD_EXPAND_VAR_BRACED)
    {
        tcl_error_set(error, TCL_ERROR_SYNTAX, word->span.line, word->span.column, "argument expansion is not supported");
        return 0;
    }

    return resolve_string_text(context, word->text, &word->span, error, result);
}

int evaluate_expansion_word(
    ExecutorContext *context,
    const AstWord *word,
    TclError *error,
    char ***items,
    size_t *item_count)
{
    (void)context;
    (void)items;
    (void)item_count;

    tcl_error_set(error, TCL_ERROR_SYNTAX, word->span.line, word->span.column, "argument expansion is not supported");
    return 0;
}

int evaluate_words(
    ExecutorContext *context,
    const AstWord *head,
    size_t count,
    TclError *error,
    char ***values,
    size_t *value_count)
{
    const AstWord *word = head;
    char **evaluated;
    size_t index;

    evaluated = (char **)calloc(count == 0 ? 1 : count, sizeof(char *));
    if (!evaluated)
    {
        tcl_error_set(error, TCL_ERROR_SYSTEM, 1, 1, "out of memory");
        return 0;
    }

    for (index = 0; index < count; index++)
    {
        if (!evaluate_word(context, word, error, &evaluated[index]))
        {
            free_values(evaluated, index);
            return 0;
        }

        word = word->next;
    }

    *values = evaluated;
    *value_count = count;
    return 1;
}

static int parse_list_braced_item(const char *text, size_t *index, size_t length, char **item)
{
    int depth = 1;
    size_t start;

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
                *item = (char *)malloc(*index - start + 1);
                if (!*item)
                {
                    return 0;
                }

                memcpy(*item, text + start, *index - start);
                (*item)[*index - start] = '\0';
                (*index)++;
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

        if (text[*index] == '\\' && *index + 1 < length)
        {
            char decoded;
            size_t consumed;
            if (!decode_escape(text, length, *index, &decoded, &consumed))
            {
                free(buffer);
                return 0;
            }

            buffer[write_index++] = decoded;
            *index += consumed;
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
        if (text[*index] == '\\' && *index + 1 < length)
        {
            char decoded;
            size_t consumed;
            if (!decode_escape(text, length, *index, &decoded, &consumed))
            {
                free(buffer);
                return 0;
            }

            buffer[write_index++] = decoded;
            *index += consumed;
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
